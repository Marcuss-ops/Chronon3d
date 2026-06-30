#pragma once

// ============================================================================
// include/chronon3d/core/execution/scope_error.hpp
//
// §9.1 — Structured error type for ExecutionScope construction.
//
// §9.1 — ScopeErrorCode / ScopeError types have been extracted to
// `execution_scope_types.hpp` (shared with execution_scope.hpp).
// This header remains as a compatibility shim that re-exports
// everything via the canonical types header.
// ============================================================================

#include <chronon3d/core/execution/execution_scope_types.hpp>

// Types formerly defined here are now in execution_scope_types.hpp:
//   - ScopeErrorCode
//   - ScopeError
//   - scope_error_code_name()
// Include this header for backward compatibility; new code should
// include execution_scope_types.hpp directly.
