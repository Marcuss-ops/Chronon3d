#include "gpu_text_cached_policy.hpp"

namespace chronon3d::graph::text_run::detail {

CachedTextExecutionPath resolve_cached_text_execution_path(
    const bool unsupported_animation,
    const bool force_software,
    const bool prepared_cache_available,
    const bool backend_available,
    const bool surface_registry_available) noexcept {
    if (unsupported_animation) {
        return CachedTextExecutionPath::RejectUnsupportedAnimation;
    }
    if (force_software) {
        return CachedTextExecutionPath::RejectForcedSoftware;
    }
    if (prepared_cache_available && backend_available && surface_registry_available) {
        return CachedTextExecutionPath::TryMtsdf;
    }
    return CachedTextExecutionPath::PackedBitmap;
}

} // namespace chronon3d::graph::text_run::detail
