// ==============================================================================
// include/chronon3d/registry/resolved_plan.hpp
//
// `resolved_plan` diagnostic (ADR-029 forward-point (e) / (g)).  Resolves a
// visual preset id to its CONCRETE style + layout + animation WITHOUT
// rendering, so a failing frame can be compared as:
//
//   expected preset  vs  resolved preset
//
// The resolver composes the three canonical resolvers already shipped:
//
//   VisualPresetRegistry  →  preset defaults
//   StyleResolver         →  ResolvedVisualStyle (defaults + job overrides)
//   OverlayLayoutResolver →  ResolvedOverlayLayout (intent → x/y, safe area,
//                            collision + fallback)
//
// plus the preset's animation intent.  It is PURE (no TextDefinition, no
// backends): the caller supplies the text content box (width × height) —
// which the render-plan compiler derives from the materialization preset —
// so this diagnostic stays dependency-light and trivially testable.
// ==============================================================================

#pragma once

#include <chronon3d/layout/overlay_layout_resolver.hpp>  // ResolvedOverlayLayout
#include <chronon3d/registry/style_resolver.hpp>         // ResolvedVisualStyle
#include <chronon3d/registry/visual_preset_descriptor.hpp>  // AnimationSpec

#include <string>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

namespace chronon3d::render_plan {
struct LayerStylePlan;  // fwd — per-job style overrides.
struct AnchorPlan;      // fwd — per-job anchor override.
}

namespace chronon3d::registry {

class VisualPresetRegistry;

// Fully-resolved projection of one overlay for diagnostics.  Every field is
// concrete (no indirection): this is what a worker actually renders.
struct ResolvedPlan {
    std::string preset_id;
    std::string semantic_role;

    ResolvedVisualStyle style;
    chronon3d::layout::ResolvedOverlayLayout layout;
    AnimationSpec animation;
};

// Resolve a preset to its concrete style/layout/animation.  Throws
// std::runtime_error on an unknown preset id (matching
// VisualPresetRegistry::get).  `box_width`/`box_height` are the text content
// bounds in canvas pixels; `style_overrides`/`anchor_override` are the
// optional per-job render-plan overrides (may be null).
[[nodiscard]] ResolvedPlan resolve_plan_diagnostic(
    const VisualPresetRegistry& registry,
    std::string_view preset_id,
    std::string_view semantic_role,
    float canvas_width,
    float canvas_height,
    float box_width,
    float box_height,
    const render_plan::LayerStylePlan* style_overrides = nullptr,
    const render_plan::AnchorPlan* anchor_override = nullptr);

// Diagnostic JSON projection (the `resolved_plan` the ADR describes).
[[nodiscard]] nlohmann::json to_json(const ResolvedPlan& plan);

} // namespace chronon3d::registry
