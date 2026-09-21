#pragma once

namespace chronon3d::graph::detail {

enum class ClearExecutionPath {
    Software,
    NativeSurface,
    Reject,
};

/// Decide whether ClearNode may attempt the native transparent surface path,
/// or must fail before building a software frame in strict native mode.
/// Allocation/initialization failures remain a runtime concern of execute().
[[nodiscard]] ClearExecutionPath resolve_clear_execution_path(
    bool require_native_gpu,
    bool backend_supports_native_surface,
    bool backend_available,
    bool surface_registry_available) noexcept;

} // namespace chronon3d::graph::detail
