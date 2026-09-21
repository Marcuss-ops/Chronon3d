#pragma once

namespace chronon3d::graph::text_run::detail {

enum class TextExecutionRoute {
    NativeMtsdf,
    NativePacked,
    Software,
    Reject,
};

enum class TextRouteReason {
    UnsupportedAnimation,
    ForcedSoftware,
    NativeResourcesAvailable,
    NativeResourcesUnavailable,
};

struct TextRouteDecision {
    TextExecutionRoute route{TextExecutionRoute::Reject};
    TextRouteReason reason{TextRouteReason::UnsupportedAnimation};
};

[[nodiscard]] const char* text_execution_route_name(
    TextExecutionRoute route) noexcept;

[[nodiscard]] const char* text_route_reason_name(
    TextRouteReason reason) noexcept;

/// Resolve the one text execution route before resources are touched. The
/// caller consumes the decision mechanically; unsupported animation is a
/// reject, while explicit software selection is observable rather than a
/// silent GPU fallback.
[[nodiscard]] TextRouteDecision resolve_text_execution_route(
    bool unsupported_animation,
    bool force_software,
    bool prepared_cache_available,
    bool backend_available,
    bool surface_registry_available) noexcept;

} // namespace chronon3d::graph::text_run::detail
