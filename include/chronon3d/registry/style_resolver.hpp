// ==============================================================================
// include/chronon3d/registry/style_resolver.hpp
//
// StyleResolver (ADR-029 forward-point (b)): merges a preset's default
// `VisualStyle` with the per-job render-plan style OVERRIDES into a single
// concrete `ResolvedVisualStyle`:
//
//   preset defaults + job overrides = ResolvedVisualStyle
//
// A job therefore ships only the fields it deviates from (never a full copy
// of the ~25-property style); the preset's `VisualStyle` fills every absent
// field.  Empty strings / absent optionals mean "leave the base text
// preset's value", so the resolver never clobbers typography that the
// underlying materialization preset already set.
//
// The resolver is PURE (no TextDefinition, no backends): it only merges
// value types, so it links into the dependency-light registry module and is
// trivially testable.  Translating a ResolvedVisualStyle onto a
// TextDefinition is the render-plan compiler's job
// (render_plan_compiler.cpp), not this module's.
// ==============================================================================

#pragma once

#include <chronon3d/registry/visual_preset_descriptor.hpp>  // VisualStyle

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::render_plan {
struct LayerStylePlan;  // fwd — transport override shape (render_plan.hpp).
}

namespace chronon3d::registry {

class VisualPresetRegistry;

// Concrete, fully-merged paint recipe.  Each field is either the job
// override, the preset default, or "leave the base preset" (empty string /
// absent optional).  The enabled flags make the stroke/shadow/background
// presence explicit so a future `resolved_plan` diagnostic can serialize
// them without re-deriving presence from field emptiness.
struct ResolvedVisualStyle {
    // Typography — empty/nullopt = leave the base text preset's value.
    std::string font_family;
    // Canonical font asset logical path (byte-identity anchor for the asset
    // manifest).  Empty = no preset default; the base text preset's own
    // font_path remains authoritative.
    std::string font_asset;
    std::optional<int> font_weight;
    std::optional<float> font_size;

    // Fill — empty = leave the base text preset's color.
    std::string fill;

    // Stroke
    bool stroke_enabled{false};
    std::string stroke_color;
    std::optional<float> stroke_width;

    // Shadow
    bool shadow_enabled{false};
    std::string shadow_color;
    std::optional<float> shadow_opacity;
    std::optional<float> shadow_blur;
    std::optional<std::array<float, 2>> shadow_offset;

    // Background card
    bool background_enabled{false};
    std::string background_color;
    std::optional<float> background_opacity;
    std::optional<float> radius;
    std::optional<std::array<float, 2>> padding;
};

// Canonical style merge.  Field-level: an override field wins when present
// (non-empty string / set optional), otherwise the preset default wins,
// otherwise the field stays "leave base" (empty/nullopt).
class VisualStyleResolver {
public:
    [[nodiscard]] ResolvedVisualStyle resolve(
        const VisualStyle& defaults,
        const render_plan::LayerStylePlan* overrides) const;

    // Resolve a preset by id from the registry.  Throws std::runtime_error on
    // an unknown id, matching VisualPresetRegistry::get.
    [[nodiscard]] ResolvedVisualStyle resolve_preset(
        const VisualPresetRegistry& registry,
        std::string_view preset_id,
        const render_plan::LayerStylePlan* overrides) const;
};

} // namespace chronon3d::registry
