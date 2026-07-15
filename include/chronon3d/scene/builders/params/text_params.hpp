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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {

struct TextLayoutSpec {
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
};

struct TextAppearanceSpec {
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
    TextContent content;
    TextPlacement placement{};
    FontSpec font;
    TextLayoutSpec layout;
    TextAppearanceSpec appearance;
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
