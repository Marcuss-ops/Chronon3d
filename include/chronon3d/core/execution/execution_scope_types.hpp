#pragma once

// ============================================================================
// include/chronon3d/core/execution/execution_scope_types.hpp
//
// §9.1 — Shared type definitions for ExecutionScope construction and errors.
//
// Extracted from execution_scope.hpp and scope_error.hpp so both headers
// can consume the canonical types without a circular dependency.  This
// header depends on NO internal headers beyond node_identity (for
// GraphInstanceId) and types (for u32).
//
// Types defined here:
//   - ExecutionScopeKind       — Root / Tile / Precomp discriminant
//   - kMaxScopeChainLength     — hard upper bound on scope chain depth (16)
//   - ScopeErrorCode           — categorical failure codes for factories
//   - ScopeError               — structured error payload (POD aggregate)
//   - execution_scope_kind_name() — stable string-form name for each kind
//   - scope_error_code_name()  — stable string-form name for each code
// ============================================================================

#include <chronon3d/core/types/types.hpp>
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
inline constexpr int kMaxScopeChainLength = 16;

// ────────────────────────────────────────────────────────────────────────────
//  ScopeErrorCode + ScopeError  (§9.1 — extracted from scope_error.hpp)
// ────────────────────────────────────────────────────────────────────────────

/// Categorical failure code for ExecutionScope factory attempts.
///
/// `make_root` / `make_child` factories (§9.2-9.5) return
/// `Result<ExecutionScope, ScopeError>` — the code identifies which
/// invariant was violated so the caller can route to the right
/// diagnostic and deterministic fallback.
enum class ScopeErrorCode : std::uint8_t {
    InvalidRootKind     = 0,  // make_root called with kind != Root
    InvalidChildKind    = 1,  // make_child called with kind == Root
    ParentRequired      = 2,  // make_child given nullptr parent
    ArenaAliasesParent  = 3,  // child arena aliases parent.arena()
    ChainLimitExceeded  = 4,  // requested chain length exceeds kMaxScopeChainLength
    RecursiveOwner      = 5,  // Precomp child whose owner_key already in parent chain
};

/// Structured error payload accompanying ScopeErrorCode.
///
/// Aggregate (no user-provided ctor) so callers can use designated
/// initialisers: `ScopeError{ .code = …, .kind = …, … }`.  All fields
/// have default member initialisers so an uninitialised `ScopeError`
/// is well-defined (`code = InvalidRootKind`, all others zero).
struct ScopeError {
    ScopeErrorCode      code{ScopeErrorCode::InvalidRootKind};
    ExecutionScopeKind  kind{ExecutionScopeKind::Root};
    GraphInstanceId     graph{kInvalidGraphInstanceId};
    std::uint64_t       owner_key{0u};
    ::chronon3d::u32      requested_chain_length{0u};
};

// ── Stable string-form name helpers ───────────────────────────────────────

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

/// Stable string-form name for each ScopeErrorCode (for logging /
/// diagnostics; never for equality).
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
