#pragma once

namespace chronon3d::graph::text_run::detail {

enum class TextExecutionRoute {
    NativeMtsdf,
    Software,
    Reject,
};

enum class TextRouteReason {
    UnsupportedAnimation,
    ForcedSoftware,
    NativeSurfacesUnavailable,
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
///
/// `native_surfaces_supported` is a lane capability, not a resource: every
/// the canonical MTSDF text kernel needs a resident glyph surface, so a
/// backend that cannot host one has no GPU text route at all and the canonical
/// software renderer owns the frame.  Resolving it here keeps that hand-off
/// explicit instead of paying a failed glyph-surface acquisition every frame.
[[nodiscard]] TextRouteDecision resolve_text_execution_route(
    bool unsupported_animation,
    bool native_surfaces_supported,
    bool force_software,
    bool prepared_cache_available,
    bool backend_available,
    bool surface_registry_available) noexcept;

} // namespace chronon3d::graph::text_run::detail
