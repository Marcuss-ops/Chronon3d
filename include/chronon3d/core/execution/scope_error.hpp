#pragma once

// ============================================================================
// include/chronon3d/core/execution/scope_error.hpp
//
// §9.1 — Structured error type for ExecutionScope construction.
//
// Replaces the implicit silent-degrade behaviour of the legacy
// `ExecutionScope` ctors (parent == nullptr silently clamps `depth` to
// 0; depth > kMaxScopeDepth silently clamps and forces callers to
// poll `would_overflow()`; an intermediate incomplete state between
// ctor and `set_owner_key(...)` calls) with a typed error report that
// the upcoming `make_root` / `make_child` factories (PR 6.8 / §9.2-9.5)
// will propagate via `chronon3d::Result<ExecutionScope, ScopeError>`.
//
// This header is intentionally header-only — no .cpp needed (the type
// is a POD aggregate with literal types).  It depends on the existing
// canonical public surface only:
//   - `ExecutionScopeKind`            (chronon3d::core/scope/execution_scope.hpp)
//   - `kMaxScopeDepth` / `kMaxScopeChainLength` (same header)
//   - `GraphInstanceId`               (chronon3d::render_graph/core/node_identity.hpp)
//   - `u32`                           (chronon3d::core/types/types.hpp)
// and introduces NO new public surface beyond this header.  The new
// constant `kMaxScopeChainLength` lands in §9.5 as part of the factory
// migration step; this header references the existing `kMaxScopeDepth`
// boundary only in documentation, never in code, so renaming the bound
// in §9.5 does not require touching this file again.
//
// PR 6.8 split-out (NO new factory code in this PR — only the error
// type and its stable name helper):
//   1. The six `ScopeErrorCode` discriminants enumerate every failure
//      path the new factories must report (replaces silent-clamp +
//      silent-parent-null + would_overflow accessor + would_recurse
//      post-check).
//   2. `ScopeError` aggregates the categorical code with context
//      payload (attempted kind, bound graph id, attempted owner key,
//      requested chain length).  POD so `Result<…, ScopeError>` can
//      move-construct the error without surprise.
// ============================================================================

#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>

#include <cstdint>

namespace chronon3d::graph {

/// Categorical failure code for `ExecutionScope` factory attempts.
///
/// Future `make_root` / `make_child` factories (§9.2-9.5) will return
/// `Result<ExecutionScope, ScopeError>` — the code identifies which
/// invariant was violated so the caller (Root executor, tile executor,
/// PrecompNode) can route to the right diagnostic and deterministic
/// fallback.  The discriminant order is the stable contract; do NOT
/// reorder existing entries (add new ones at the tail).
///
/// Coverage:
///   - `InvalidRootKind`        — `make_root` called with `kind != Root`
///                                (turning a root request into a child
///                                 silently is a usage contract violation
///                                 and must surface structurally).
///   - `InvalidChildKind`       — `make_child` called with `kind == Root`
///                                (a root-as-child chain would corrupt
///                                 the lease / cache invariants).
///   - `ParentRequired`         — `make_root` was given a parent pointer
///                                OR `make_child` was given a null parent.
///                                (Both are programmer errors today
///                                 silently coerced to depth 0.)
///   - `ArenaAliasesParent`     — child's `FrameArena&` aliases
///                                `parent.arena()`.  Child teardown
///                                would reset an arena the parent still
///                                holds pointers into — forbidden.
///   - `ChainLimitExceeded`     — requested chain length exceeds the
///                                16-bound at ctor time.  Replaces PR 6.5
///                                silent-clamp + `would_overflow()`.
///   - `RecursiveOwner`         — Precomp child whose `owner_key`
///                                already appears in the active parent
///                                chain (direct or indirect).  Replaces
///                                PR 6.5 post-`would_recurse` check.
enum class ScopeErrorCode : std::uint8_t {
    InvalidRootKind     = 0,
    InvalidChildKind    = 1,
    ParentRequired      = 2,
    ArenaAliasesParent  = 3,
    ChainLimitExceeded  = 4,
    RecursiveOwner      = 5,
};

/// Structured error payload accompanying `ScopeErrorCode`.
///
/// Aggregate (no user-provided ctor) so callers can use designated
/// initialisers: `ScopeError{ .code = …, .kind = …, … }`.  All fields
/// have default member initialisers so an uninitialised `ScopeError`
/// is well-defined (`code = InvalidRootKind`, all others zero).
///
/// Fields:
///   `code`                    — categorical code (see enum above).
///   `kind`                    — the `ExecutionScopeKind` the caller
///                               attempted to construct
///                               (`Root` / `Tile` / `Precomp`).
///   `graph`                   — the `GraphInstanceId` the caller
///                               attempted to bind at construction.
///                               Mostly diagnostic; useful when two
///                               sibling scopes race on the same id.
///   `owner_key`               — owner key the caller attempted to
///                               register (only meaningful for `Precomp`;
///                               `0` otherwise).  Lets the caller
///                               identify which precomp caused the
///                               `RecursiveOwner` failure.
///   `requested_chain_length`  — chain length the would-be scope would
///                               have had (`root == 1`,
///                               `root + 1 child == 2`, …).  Lets the
///                               caller compare to the post-§9.5 bound
///                               (`kMaxScopeChainLength`) without
///                               reading internal state.
struct ScopeError {
    ScopeErrorCode      code{ScopeErrorCode::InvalidRootKind};
    ExecutionScopeKind  kind{ExecutionScopeKind::Root};
    GraphInstanceId     graph{kInvalidGraphInstanceId};
    std::uint64_t       owner_key{0u};
    chronon3d::u32      requested_chain_length{0u};
};

/// Stable string-form name for each `ScopeErrorCode` (for logging /
/// diagnostics; never for equality — the enum's storage type may
/// evolve, order is the contract).  Mirrors the
/// `execution_scope_kind_name` helper already exported next to
/// `ExecutionScopeKind` so diagnostic sinks can uniformly stringify
/// both kinds and codes without conditional compilation.
inline const char* scope_error_code_name(ScopeErrorCode c) noexcept {
    switch (c) {
        case ScopeErrorCode::InvalidRootKind:    return "InvalidRootKind";
        case ScopeErrorCode::InvalidChildKind:   return "InvalidChildKind";
        case ScopeErrorCode::ParentRequired:     return "ParentRequired";
        case ScopeErrorCode::ArenaAliasesParent: return "ArenaAliasesParent";
        case ScopeErrorCode::ChainLimitExceeded: return "ChainLimitExceeded";
        case ScopeErrorCode::RecursiveOwner:     return "RecursiveOwner";
    }
    return "Unknown";
}

} // namespace chronon3d::graph
