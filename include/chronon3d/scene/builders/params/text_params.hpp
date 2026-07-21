#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>
#include <chronon3d/text/paragraph_style.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_direction.hpp>
#include <chronon3d/text/text_placement.hpp>
#include <chronon3d/text/text_span_override.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {

struct TextLayoutSpec {
    // Per-field phase tags (TICKET-TEXT-PROPERTY-PHASES — PreLayout = reflow): box/anchor/align/wrap/overflow/line_height/tracking/auto_fit/min/max_font_size/max_lines/ellipsis/paragraph/centering_mode/vertical_align. The runtime animated variants (TextAnimator::*) override these via PostLayout on a per-glyph basis.
    Vec2 box{900.0f, 160.0f};
    TextAnchor anchor{TextAnchor::Center};
    TextCenteringMode centering_mode{TextCenteringMode::LayoutBox};
    TextAlign align{TextAlign::Center};
    VerticalAlign vertical_align{VerticalAlign::Middle};
    TextWrap wrap{TextWrap::Word};
    TextOverflow overflow{TextOverflow::Clip};
    f32 line_height{1.2f};
    f32 tracking{0.0f};
    bool auto_fit{false};
    f32 min_font_size{12.0f};
    f32 max_font_size{160.0f};
    i32 max_lines{0};
    bool ellipsis{false};
    ParagraphStyle paragraph{};

    /// OpenType / HarfBuzz shaping features (e.g. "kern=1,liga=1,calt=0").
    /// Empty default = use the font's natural feature set (HarfBuzz
    /// applies its implicit defaults: kern=1, liga=1, calt=1, ...).
    std::string features{};
};

struct TextAppearanceSpec {
    // Per-field phase tags (TICKET-TEXT-PROPERTY-PHASES — PostLayout = visual-only): color/paint/shadows/material/box_style. Animations targeting these properties apply per-glyph at the PostLayout stage and do NOT invalidate layout/shaping cache.
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    TextPaint paint{};
    std::vector<TextShadow> shadows{};
    TextMaterial material{};
    TextBoxStyle box_style{};
};

struct TextContent {
    std::string value;
    std::shared_ptr<PlacedGlyphRun> pre_shaped;
};

struct TextSpec {
    // Per-field phase tags (TICKET-TEXT-PROPERTY-PHASES):
    //   .content     → PreShaping substrate (value/pre_shaped are inputs to the PreShaping evaluator)
    //   .placement   → PreLayout (reflow; affects paragraph composition)
    //   .font        → PreLayout (reflow; font size feeds HarfBuzz shaping metrics)
    //   .layout      → PreLayout (reflow; see TextLayoutSpec table above)
    //   .appearance  → PostLayout (visual-only; see TextAppearanceSpec table above)
    //   .spans[]     → mixed phase; per-span font is PreLayout, per-span color is PostLayout.
    TextContent content;
    TextPlacement placement{};
    FontSpec font;
    TextLayoutSpec layout;
    TextAppearanceSpec appearance;

    /// Per-range style overrides for rich text.  Each override covers
    /// the UTF-8 byte range [byte_start, byte_end) inside content.value.
    /// Empty vector means "single-style text".
    std::vector<TextSpanOverride> spans{};
};

// Source-compatible aliases retained until the external migration closes.
using TextParams = TextSpec;

struct TextRunSpec {
    TextSpec text;
    TextDirection direction{TextDirection::Auto};
    std::string language;
    std::uint32_t script{0u};
    std::vector<TextAnimatorSpec> animators;
    std::vector<GlyphSelectorSpec> selectors;
    bool cache_layout{true};
};

using TextRunParams = TextRunSpec;

struct ShadowStyle {
    TextShadow contact{
        .enabled = true,
        .offset = {0.0f, 6.0f},
        .blur = 14.0f,
        .opacity = 0.28f,
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    };
    TextShadow ambient{
        .enabled = true,
        .offset = {0.0f, 40.0f},
        .blur = 120.0f,
        .opacity = 0.08f,
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    };
};

} // namespace chronon3d
