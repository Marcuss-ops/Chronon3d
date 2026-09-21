#include "gpu_text_cached_policy.hpp"

namespace chronon3d::graph::text_run::detail {

const char* text_execution_route_name(
    const TextExecutionRoute route) noexcept {
    switch (route) {
        case TextExecutionRoute::NativeMtsdf: return "native_mtsdf";
        case TextExecutionRoute::NativePacked: return "native_packed";
        case TextExecutionRoute::Software: return "software";
        case TextExecutionRoute::Reject: return "reject";
    }
    return "unknown";
}

const char* text_route_reason_name(
    const TextRouteReason reason) noexcept {
    switch (reason) {
        case TextRouteReason::UnsupportedAnimation: return "unsupported_animation";
        case TextRouteReason::ForcedSoftware: return "forced_software";
        case TextRouteReason::NativeSurfacesUnavailable: return "native_surfaces_unavailable";
        case TextRouteReason::NativeResourcesAvailable: return "native_resources_available";
        case TextRouteReason::NativeResourcesUnavailable: return "native_resources_unavailable";
    }
    return "unknown";
}

TextRouteDecision resolve_text_execution_route(
    const bool unsupported_animation,
    const bool native_surfaces_supported,
    const bool force_software,
    const bool prepared_cache_available,
    const bool backend_available,
    const bool surface_registry_available) noexcept {
    if (unsupported_animation) {
        return {TextExecutionRoute::Reject, TextRouteReason::UnsupportedAnimation};
    }
    if (force_software) {
        return {TextExecutionRoute::Software, TextRouteReason::ForcedSoftware};
    }
    // Both native kernels draw into a resident glyph surface.  A lane that
    // cannot host one is a software-text lane for this frame even though the
    // prepared cache and surface registry are present: choosing the native
    // route there would acquire the atlas, fail the dispatch, and repeat the
    // whole failure on the next frame.
    if (!native_surfaces_supported) {
        return {TextExecutionRoute::Software,
                TextRouteReason::NativeSurfacesUnavailable};
    }
    if (prepared_cache_available && backend_available && surface_registry_available) {
        return {TextExecutionRoute::NativeMtsdf,
                TextRouteReason::NativeResourcesAvailable};
    }
    return {TextExecutionRoute::NativePacked,
            TextRouteReason::NativeResourcesUnavailable};
}

} // namespace chronon3d::graph::text_run::detail
