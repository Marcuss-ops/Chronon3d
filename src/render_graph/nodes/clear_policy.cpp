#include "clear_policy.hpp"

namespace chronon3d::graph::detail {

ClearExecutionPath resolve_clear_execution_path(
    bool require_native_gpu,
    bool backend_supports_native_surface,
    bool backend_available,
    bool surface_registry_available) noexcept {
    const bool native_requested =
        require_native_gpu || backend_supports_native_surface;
    if (native_requested && backend_available && surface_registry_available) {
        return ClearExecutionPath::NativeSurface;
    }
    return require_native_gpu
        ? ClearExecutionPath::Reject
        : ClearExecutionPath::Software;
}

} // namespace chronon3d::graph::detail
