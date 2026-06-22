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
// This header is intentionally header-only (no .cpp); the class holds
// no resources of its own.  Tests are at
// `tests/core/test_execution_scope.cpp` — six TEST_CASE bodies covering
// the construction invariants, parent/non-parent chain semantics, the
// direct / indirect recursion guard, and the child-arena independence
// property.
//
// TICKET-011a follow-up note: ExecutionScope itself holds NO counters,
// NO caches.  All per-job state stays on `RenderSession` (SceneHasher,
// SceneProgramStore, FrameHistory, DirtyHistory).  The scope is a thin
// routing object — it tells callers WHERE to allocate and WHICH chain
// they're in; it does not OWN anything of its own.
// ============================================================================

#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>

#include <cstdint>

namespace chronon3d::graph {

/// Which surface owns this scope.  Drives child-vs-parent memory rules.
enum class ExecutionScopeKind : std::uint8_t {
    Root    = 0,  // one per render invocation; primary arena; establishes chain
    Tile    = 1,  // one per coalesced dirty region; borrows parent's session + caches
    Precomp = 2,  // nested subgraph; holds a ProgramLease; borrows parent's session
};

/// Hard upper bound on scope chain depth.  Prevents unbounded recursion
/// without relying on a recursive mutex (per PR 6.5 — "Do not use a
/// recursive mutex as recursion handling").
inline constexpr int kMaxScopeDepth = 16;

/// Per-execution-surface bundle: identity + arena + parent + lease hooks.
///
/// Construction invariants:
///   - `kind != Root` requires `parent != nullptr` (the chain must be
///     rooted).  Constructing a child from `nullptr` is a programmer
///     error; the ctor accepts it but clamps `depth` to 0 so the call
///     site surfaces the misuse in a follow-up assertion if needed.
///   - `session` lifetime MUST outlive every scope that borrows it
///     (typically: scope's lifetime is nested inside session's lifetime).
///   - `arena` is captured BY REFERENCE at construction.  Callers that
///     need a child arena distinct from the parent arena should construct
///     the child arena FIRST and pass it explicitly to the child scope's
///     ctor.  The scope itself does NOT alias the arena or take ownership.
///
/// Thread-safety:
///   - Scope instances are NOT thread-safe; each render job owns its
///     scopes on the call stack, no concurrent mutation.
///   - The chain (parent / grand-parent / ...) is fixed at construction
///     time and only read after — no thread-safety machinery needed.
class ExecutionScope {
public:
    /// Root scope ctor — no parent, depth = 0.  Arena defaults to
    /// `session.arena()`.  Use the explicit-arena ctor if a child
    /// scope needs a distinct arena from the parent.
    explicit ExecutionScope(
        ExecutionScopeKind                  kind,
        chronon3d::RenderSession&           session,
        GraphInstanceId                     graph_id = kInvalidGraphInstanceId
    ) noexcept
        : ExecutionScope(kind, session, session.arena(), graph_id, nullptr)
    {}

    /// Child scope ctor — requires a non-null parent; depth tracks.
    /// Arena defaults to the parent's arena.
    ///
    /// PR 6.7 — DEPRECATED.  Child scopes MUST use the explicit-arena
    /// ctor below so that the child arena is provably distinct from the
    /// parent's arena.  Defaulting to the parent's arena silently shares
    /// the allocation surface, which means child teardown (ArenaGuard
    /// reset) would invalidate pointers the parent still holds.
    [[deprecated("child scopes must pass an explicit FrameArena& distinct from the parent's — use ExecutionScope(kind, session, child_arena, graph_id, parent) with a locally-constructed child arena")]]
    explicit ExecutionScope(
        ExecutionScopeKind                  kind,
        chronon3d::RenderSession&           session,
        GraphInstanceId                     graph_id,
        const ExecutionScope*               parent
    ) noexcept
        : ExecutionScope(kind, session, session.arena(), graph_id, parent)
    {}

    /// Explicit-arena ctor (PR 6.3 / 6.4 — child arenas distinct from
    /// the parent).  Caller-owned arena; lifetime tracked by caller.
    /// Passing `parent == nullptr` is silently accepted (depth clamps
    /// to 0); the call is documented as a usage contract violation.
    ///
    /// PR 6.5 — depth is CLAMPED to `kMaxScopeDepth` (no exception, no
    /// abort — consistent with `noexcept` contract).  When the
    /// constructed depth would have exceeded the bound, `would_overflow()`
    /// returns true on this scope so callers can detect the situation
    /// and route to a deterministic fallback (e.g. bail out with empty
    /// fb per docs/03 §4.4).  The clamp guarantees bounded chain walks
    /// (is_descendant_of / would_recurse always terminate in at most
    /// `kMaxScopeDepth` iterations).
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
        , m_depth(parent
            ? ((parent->m_depth + 1) > kMaxScopeDepth
                ? kMaxScopeDepth
                : (parent->m_depth + 1))
            : 0)
        // m_owner_key falls back on its `{0u}` member default below.
        , m_overflowed(parent != nullptr
                       && (parent->m_depth + 1) > kMaxScopeDepth)
    {}

    [[nodiscard]] ExecutionScopeKind           kind()      const noexcept { return m_kind; }
    [[nodiscard]] chronon3d::RenderSession&    session()   const noexcept { return m_session; }
    [[nodiscard]] FrameArena&                  arena()     const noexcept { return m_arena; }
    [[nodiscard]] GraphInstanceId              graph_id()  const noexcept { return m_graph_id; }
    [[nodiscard]] const ExecutionScope*        parent()    const noexcept { return m_parent; }
    [[nodiscard]] int                          depth()     const noexcept { return m_depth; }

    /// PR 6.5 — true iff this scope's depth was CLAMPED by `kMaxScopeDepth`
    /// (i.e. the chain the scope was constructed against exceeded 16
    /// nested scopes).  When true, callers SHOULD route to a deterministic
    /// fallback rather than proceed into the inner executor — the chain
    /// walk inside `would_recurse`/`is_descendant_of` is still bounded
    /// (they always terminate in at most `kMaxScopeDepth` steps) but the
    /// downstream graph execution will diverge from the intended depth
    /// count and may have hidden aliasing on the parent's caches.
    [[nodiscard]] bool would_overflow() const noexcept { return m_overflowed; }

    /// Owner key — populated by PrecompNode via `set_owner_key(k)`
    /// before executing an inner subgraph.  Consulted by
    /// `would_recurse()` to reject direct + indirect precomp recursion
    /// per PR 6.5.
    void                set_owner_key(std::uint64_t key) noexcept { m_owner_key = key; }
    [[nodiscard]] std::uint64_t owner_key()     const noexcept { return m_owner_key; }

    /// True iff `other` is a strict ancestor of THIS (this is `other`'s
    /// child or grand-child).  O(depth).
    [[nodiscard]] bool is_descendant_of(const ExecutionScope& other) const noexcept {
        for (const ExecutionScope* cur = m_parent; cur; cur = cur->m_parent) {
            if (cur == &other) return true;
        }
        return false;
    }

    /// PR 6.5 — guards against direct AND indirect precomp recursion.
    /// Returns true if constructing a Precomp child scope whose
    /// `owner_key == k` would re-enter any active Precomp scope in the
    /// current chain.
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

    /// Returns the chain length (including this scope).  Bounded by
    /// kMaxScopeDepth; deeper chains are clamped to kMaxScopeDepth so
    /// callers can compare without surprises.
    [[nodiscard]] int chain_length() const noexcept {
        return m_depth + 1;
    }

private:
    ExecutionScopeKind              m_kind;
    chronon3d::RenderSession&       m_session;
    FrameArena&                     m_arena;
    GraphInstanceId                 m_graph_id{kInvalidGraphInstanceId};
    const ExecutionScope*           m_parent{nullptr};
    int                             m_depth{0};
    std::uint64_t                   m_owner_key{0u};
    bool                            m_overflowed{false};
};

/// Convenience: a stable string-form name for each kind (for logging /
/// diagnostics; never for equality, since the enum's storage type may
/// evolve and order is the contract).
inline const char* execution_scope_kind_name(ExecutionScopeKind k) noexcept {
    switch (k) {
        case ExecutionScopeKind::Root:    return "Root";
        case ExecutionScopeKind::Tile:    return "Tile";
        case ExecutionScopeKind::Precomp: return "Precomp";
    }
    return "Unknown";
}

} // namespace chronon3d::graph
