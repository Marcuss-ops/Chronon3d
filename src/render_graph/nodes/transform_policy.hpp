#pragma once

namespace chronon3d::graph::detail {

enum class TransformExecutionPath {
    Software,
    NativeSurface,
    Reject,
};

// Resolve the capability gate before the transform node allocates resources
// or dispatches a backend operation.  A native transform is possible only
// when all three runtime resources are present; strict mode turns the same
// missing capability into an explicit rejection instead of a CPU fallback.
TransformExecutionPath resolve_transform_execution_path(
    bool require_native_gpu,
    bool input_surface_valid,
    bool backend_available,
    bool surface_registry_available) noexcept;

} // namespace chronon3d::graph::detail
