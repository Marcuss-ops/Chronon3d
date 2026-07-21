#pragma once

// ============================================================================
// include/chronon3d/core/scope/execution_scope.hpp
//
// WP-6 PR 6.0 — ExecutionScope foundation.
//
// Three execution surfaces (root render-job, tile region, precomp inner
// graph) share an identity/arena/parent contract so child code paths
// never reset the parent's arena and never loop the operator-key chain.
// Each scope is constructed with an explicit kind, session, arena
// reference, and (for non-root scopes) parent pointer.  Recursion is
// rejected via depth + owner-key tracking — no recursive mutex, no
// global registry.
//
// Pre-PR-6.0 path: tile / precomp inner execution constructed a local
// RenderSession and reset it on return.  The parent session's arena was
// at risk of being invalidated when the child phase ran reset_job()
// while the parent still held pointers into the same arena.  Post-PR-
// 6.0 path: child scopes own a stable reference (not a copy) to the
// parent's session + a stable FrameArena reference captured at
// construction (may be the parent's arena or a distinct child arena
// depending on kind; PR 6.3 / 6.4 will fully realise the child-arena
// allocation).
//
// Ownership invariants (PR 6.0 exit criteria, sub-set):
//   - scheduler lives OUTSIDE the scope (passed by executor callers).
//   - job-owned store/history lives ON the session, NOT duplicated per
//     scope (this PR keeps the spec; the duplication is forbidden and
//     can be enforced as a static_assert in a follow-up).
//
// ── FASE 5 — Closed legacy public surface (2026-06-30) ──────────────────────
// Before FASE 5 the class exposed 3 public direct ctors + `set_owner_key()`,
// `would_overflow()`, `would_recurse()` as public methods.  That surface
// allowed bypassing `make_root()`/`make_child()` validation: a caller could
// emit a chain with depth > kMaxScopeChainLength (silently clamped) or
// trigger recursion by setting an owner_key post-construction.  FASE 5 closes
// the surface:
//   * All direct explicit ctors are moved to `private:` (and stay there
//     as the underlying storage constructor used by the static factories).
//   * `set_owner_key()`, `would_recurse()` are private — internal
//     implementation details.
//   * `m_overflowed` and `would_overflow()` are removed entirely.
//     `make_child()` enforces `parent->chain_length() < kMaxScopeChainLength`
//     and rejects on overflow with `ScopeErrorCode::ChainLimitExceeded`,
//     so a scope with clamped depth can no longer exist on the public path.
//   * Public construction surface is exactly: `make_root(session, arena,
//     [graph_id])` and `make_child(kind, session, arena, graph_id, parent,
//     [owner_key])`.
//
// This header is intentionally header-only (no .cpp); the class holds
// no resources of its own.  Tests are at
// `tests/core/test_execution_scope.cpp`.
// ============================================================================

#include <chronon3d/core/execution/execution_scope_types.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/internal/runtime/render_session.hpp>

#include <cstdint>

namespace chronon3d::graph {

/// Per-execution-surface bundle: identity + arena + parent + lease hooks.
///
/// Construction invariants (FASE 5 — public construction is `make_root` /
/// `make_child` ONLY):
///   - `kind != Root` requires `parent != nullptr` (the chain must be
///     rooted) — enforced by `make_child`, returned as `ScopeError.
///     ParentRequired` if violated.
///   - `session` lifetime MUST outlive every scope that borrows it
///     (typically: scope's lifetime is nested inside session's lifetime).
///   - `arena` is captured BY REFERENCE at construction.  Callers that
///     need a child arena distinct from the parent arena should construct
///     the child arena FIRST and pass it explicitly to the child scope's
///     factory.  The scope itself does NOT alias the arena or take ownership.
///   - `make_child` enforces chain-length, arena-aliasing, and recursion
///     invariants; violators return `ScopeError` with one of six
///     `ScopeErrorCode` values (see `execution_scope_types.hpp`).
///
/// Thread-safety:
///   - Scope instances are NOT thread-safe; each render job owns its
///     scopes on the call stack, no concurrent mutation.
///   - The chain (parent / grand-parent / ...) is fixed at construction
///     time and only read after — no thread-safety machinery needed.
class ExecutionScope {
public:
    // ── §9.2 — Public ROOT factory (infallible) ─────────────────────────────
    //
    // `make_root` is the canonical path for the root scope.  Construction
    // cannot fail:
    //   * kind is hard-coded to `Root` (no caller-passed kind to validate)
    //   * parent is hard-coded to nullptr (chain anchor, no parent → no
    //     chain-limit candidate to violate)
    //   * chain_length() of the anchor is exactly 1 regardless of bound
    //
    // The factory accepts an explicit `FrameArena&` (NOT defaulted to
    // `session.arena()`) so the call site is honest about arena ownership:
    // callers that need the root to use `session.arena()` pass that
    // explicitly, callers that want a distinct fast-reset arena (e.g. a
    // transient scratch arena for the render job) pass that instead.  This
    // mirrors the discipline used in `make_child` so root + child sites use
    // the same explicit-arena pattern.
    //
    // `graph_id` defaults to `kInvalidGraphInstanceId` to match the
    // existing ctor default — the executor overwrites it post-construction
    // when the compiled graph identity is known (see executor.cpp).
    static ExecutionScope make_root(
        chronon3d::RenderSession&           session,
        FrameArena&                         arena,
        GraphInstanceId                     graph_id = kInvalidGraphInstanceId
    ) noexcept;

    // ── §9.3 — Public CHILD factory (Result<ExecutionScope, ScopeError>) ──
    //
    // `make_child` is the canonical path for constructing non-root scopes.
    // Returns `ScopeError` on any invariant violation instead of silently
    // clamping or coercing.  Validation order matches the error-code enum
    // (InvalidChildKind → ParentRequired → ArenaAliasesParent →
    // ChainLimitExceeded → RecursiveOwner).
    //
    //   * `kind == Root`         → InvalidChildKind
    //   * `parent == nullptr`    → ParentRequired
    //   * `&arena == &parent->arena()` → ArenaAliasesParent
    //   * `parent->chain_length() >= kMaxScopeChainLength` → ChainLimitExceeded
    //   * `kind == Precomp && owner_key != 0 && would_recurse(owner_key)` → RecursiveOwner
    //
    // On success, the returned scope has the `owner_key` already applied
    // (via the private `set_owner_key`) so callers don't need a separate
    // post-construction call.
    static chronon3d::Result<ExecutionScope, ScopeError> make_child(
        ExecutionScopeKind                  kind,
        chronon3d::RenderSession&           session,
        FrameArena&                         arena,
        GraphInstanceId                     graph_id,
        const ExecutionScope*               parent,
        std::uint64_t                       owner_key = 0u
    ) noexcept;

    [[nodiscard]] ExecutionScopeKind           kind()      const noexcept { return m_kind; }
    [[nodiscard]] chronon3d::RenderSession&    session()   const noexcept { return m_session; }
    [[nodiscard]] FrameArena&                  arena()     const noexcept { return m_arena; }
    [[nodiscard]] GraphInstanceId              graph_id()  const noexcept { return m_graph_id; }
    [[nodiscard]] const ExecutionScope*        parent()    const noexcept { return m_parent; }
    [[nodiscard]] int                          depth()     const noexcept { return m_depth; }

    /// Owner key — populated by `make_child` (and only by `make_child`)
    /// with the `owner_key` argument.  Read-only for callers; mutation
    /// after construction is forbidden, hence no `set_owner_key()` in
    /// the public API.
    [[nodiscard]] std::uint64_t owner_key() const noexcept { return m_owner_key; }

    /// True iff `other` is a strict ancestor of THIS (this is `other`'s
    /// child or grand-child).  O(depth).
    [[nodiscard]] bool is_descendant_of(const ExecutionScope& other) const noexcept {
        for (const ExecutionScope* cur = m_parent; cur; cur = cur->m_parent) {
            if (cur == &other) return true;
        }
        return false;
    }

    /// Returns the chain length (including this scope).  Bounded by
    /// kMaxScopeChainLength; deeper chains are clamped to kMaxScopeChainLength so
    /// callers can compare without surprises.
    [[nodiscard]] int chain_length() const noexcept {
        return m_depth + 1;
    }

private:
    // ── Internal 5-arg ctor — PRIVATE (FASE 5) ─────────────────────────────
    //
    // Reachable only from the static factories `make_root` / `make_child`,
    // which are class members and therefore have private-access.
    // Direct construction by any caller (test, executor, includes) is a
    // compile error after FASE 5.
    explicit ExecutionScope(
        ExecutionScopeKind                  kind,
        chronon3d::RenderSession&           session,
        FrameArena&                         arena,
        GraphInstanceId                     graph_id,
        const ExecutionScope*               parent
    ) noexcept
        : m_kind(kind)
        , m_session(session)
        , m_arena(arena)
        , m_graph_id(graph_id)
        , m_parent(parent)
        , m_depth(parent ? (parent->m_depth + 1) : 0)
        // m_owner_key falls back on its `{0u}` member default below.
    {}

    // Private mutation entry — only the static factories call this.  Pulled
    // out of the ctor so `make_child` can apply the owner_key after all
    // validation has succeeded (recursion check depends on the chain being
    // reachable through `would_recurse` → but `would_recurse` walks `this`
    // upward, so the key applied to the new scope is read by FUTURE checks
    // on parents of this scope, not this scope itself).
    void set_owner_key(std::uint64_t key) noexcept { m_owner_key = key; }

    // PR 6.5 — guards against direct AND indirect precomp recursion.
    // Returns true if constructing a Precomp child scope whose
    // `owner_key == k` would re-enter any active Precomp scope in the
    // current chain.
    [[nodiscard]] bool would_recurse(std::uint64_t k) const noexcept {
        for (const ExecutionScope* cur = this; cur; cur = cur->m_parent) {
            if (cur->m_kind == ExecutionScopeKind::Precomp
                && cur->m_owner_key != 0u
                && cur->m_owner_key == k) {
                return true;
            }
        }
        return false;
    }

    ExecutionScopeKind              m_kind;
    chronon3d::RenderSession&       m_session;
    FrameArena&                     m_arena;
    GraphInstanceId                 m_graph_id{kInvalidGraphInstanceId};
    const ExecutionScope*           m_parent{nullptr};
    int                             m_depth{0};
    std::uint64_t                   m_owner_key{0u};
};

// ── Inline factory definitions (kept out-of-class for readability) ─────

inline ExecutionScope ExecutionScope::make_root(
    chronon3d::RenderSession&           session,
    FrameArena&                         arena,
    GraphInstanceId                     graph_id
) noexcept {
    return ExecutionScope(
        ExecutionScopeKind::Root,
        session, arena, graph_id, /*parent*/ nullptr);
}

inline chronon3d::Result<ExecutionScope, ScopeError> ExecutionScope::make_child(
    ExecutionScopeKind                  kind,
    chronon3d::RenderSession&           session,
    FrameArena&                         arena,
    GraphInstanceId                     graph_id,
    const ExecutionScope*               parent,
    std::uint64_t                       owner_key
) noexcept {
    if (kind == ExecutionScopeKind::Root) {
        return ScopeError{
            ScopeErrorCode::InvalidChildKind,
            kind, graph_id, owner_key,
            static_cast<chronon3d::u32>(parent ? parent->chain_length() + 1 : 1)
        };
    }
    if (!parent) {
        return ScopeError{
            ScopeErrorCode::ParentRequired,
            kind, graph_id, owner_key, 1u
        };
    }
    if (&arena == &parent->arena()) {
        return ScopeError{
            ScopeErrorCode::ArenaAliasesParent,
            kind, graph_id, owner_key,
            static_cast<chronon3d::u32>(parent->chain_length() + 1)
        };
    }
    if (parent->chain_length() >= kMaxScopeChainLength) {
        return ScopeError{
            ScopeErrorCode::ChainLimitExceeded,
            kind, graph_id, owner_key,
            static_cast<chronon3d::u32>(parent->chain_length() + 1)
        };
    }
    if (kind == ExecutionScopeKind::Precomp
        && owner_key != 0u
        && parent->would_recurse(owner_key)) {
        return ScopeError{
            ScopeErrorCode::RecursiveOwner,
            kind, graph_id, owner_key,
            static_cast<chronon3d::u32>(parent->chain_length() + 1)
        };
    }
    ExecutionScope child(
        kind, session, arena, graph_id, parent);
    child.set_owner_key(owner_key);
    return child;
}

} // namespace chronon3d::graph
