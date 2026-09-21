#include "transform_policy.hpp"

namespace chronon3d::graph::detail {

TransformExecutionPath resolve_transform_execution_path(
    const bool require_native_gpu,
    const bool input_surface_valid,
    const bool backend_available,
    const bool surface_registry_available) noexcept {
    if (input_surface_valid && backend_available && surface_registry_available) {
        return TransformExecutionPath::NativeSurface;
    }
    return require_native_gpu
        ? TransformExecutionPath::Reject
        : TransformExecutionPath::Software;
}

} // namespace chronon3d::graph::detail
