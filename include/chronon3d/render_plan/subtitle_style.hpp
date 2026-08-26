#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// render_plan/subtitle_style.hpp — SubtitleTrack consumption of layer.style
//
// Lower a layer `style` block onto the SubtitleTrackBuilder with EXACTLY the
// same semantics the text materializer (visual_preset_materializer.cpp)
// applies to text layers: fill → text color, shadow → one enabled
// TextShadow, font_size → font size. Both paths share parse_hex_color
// (color_utils.hpp), so a custom subtitle color/shadow cannot drift from the
// text materializer's interpretation.
//
// Absent fields stay std::nullopt so the caller keeps its preset/plan
// defaults — the plan never fabricates a style the request did not declare.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/render_plan/render_plan.hpp>   // LayerStylePlan / ShadowStyle
#include <chronon3d/scene/model/shape/shape.hpp>   // TextShadow

#include <optional>

namespace chronon3d::render_plan {

/// The subtitle-track projection of `LayerStylePlan`. Every field is absent
/// when the plan did not declare it (or the value is unusable, e.g. an
/// unparseable hex color), so the caller keeps its defaults.
struct ResolvedSubtitleStyle {
    std::optional<chronon3d::Color> fill;
    std::optional<float> font_size;
    std::optional<chronon3d::TextShadow> shadow;
};

/// Lower a layer `style` block for the subtitle track. Shadow defaults mirror
/// the text materializer exactly: enabled=true, opacity 1.0 when absent, blur
/// 0.0 when absent, offset from the plan (0,0) when absent, and the
/// TextShadow default color when the plan's color is absent/unparseable.
inline ResolvedSubtitleStyle resolve_subtitle_style(
    const LayerStylePlan& style) {
    ResolvedSubtitleStyle out;
    if (const auto fill = parse_hex_color(style.fill))
        out.fill = *fill;
    if (style.font_size && *style.font_size > 0.0f)
        out.font_size = *style.font_size;
    if (style.shadow) {
        chronon3d::TextShadow shadow;
        shadow.enabled = true;
        if (const auto color = parse_hex_color(style.shadow->color))
            shadow.color = *color;
        shadow.opacity = style.shadow->opacity.value_or(1.0f);
        shadow.blur = style.shadow->blur.value_or(0.0f);
        shadow.offset = {style.shadow->offset[0], style.shadow->offset[1]};
        out.shadow = shadow;
    }
    return out;
}

} // namespace chronon3d::render_plan
