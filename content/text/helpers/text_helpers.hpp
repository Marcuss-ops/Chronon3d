#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <string>

namespace chronon3d::content::text {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

struct TextDef {
    std::string  text;
    f32          size{72.0f};
    Color        color{1, 1, 1, 1};
    TextAlign    align{TextAlign::Center};
    VerticalAlign vertical_align{VerticalAlign::Top};
    f32          tracking{0.0f};
    f32          line_height{1.2f};
    std::string  font_path{"assets/fonts/Inter-Bold.ttf"};
    std::string  font_family{"Inter"};
    int          font_weight{800};
    std::string  font_style{"normal"};

    // V2 Typography
    TextPaint    paint{};
    std::vector<TextShadow> shadows{};
    TextBoxStyle box_style{};
    TextOverflow overflow{TextOverflow::Clip};
    TextWrap     wrap{TextWrap::Word};
    bool         auto_fit{false};
    bool         ellipsis{false};
    int          max_lines{0};
    f32          min_font_size{12.0f};
    f32          max_font_size{160.0f};
};

struct StyleLayer {
    TextDef      def;
    f32          y_pos{0.0f};
    Vec2         size{W * 0.7f, 160.0f};
};

void apply_text(LayerBuilder& l, const std::string& name, const TextDef& d,
                Vec2 sz = {W * 0.7f, 160.0f}, Vec3 pos = {0.0f, 0.0f, 0.0f});

/// Convert a TextStyle preset + text + geometry into a TextParams.
/// Allows using presets directly:  l.text("title", make_text_params("HELLO", presets::text::headline(), {520, 120}));
[[nodiscard]] inline TextParams make_text_params(std::string text, const TextStyle& style, Vec2 size, Vec3 pos = {0.0f, 0.0f, 0.0f}) {
    TextParams p;
    p.text = std::move(text);
    p.size = size;
    p.pos = pos;
    p.font_path = style.font_path;
    p.font_family = style.font_family;
    p.font_weight = style.font_weight;
    p.font_style = style.font_style;
    p.font_size = style.size;
    p.color = style.color;
    p.align = style.align;
    p.vertical_align = style.vertical_align;
    p.line_height = style.line_height;
    p.tracking = style.tracking;
    p.box_style = style.box_style;
    p.paint = style.paint;
    p.shadows = style.shadows;
    p.auto_fit = style.auto_fit;
    p.max_lines = style.max_lines;
    p.ellipsis = style.ellipsis;
    p.min_font_size = style.min_size;
    p.max_font_size = style.max_size;
    p.overflow = style.overflow;
    p.wrap = style.wrap;
    return p;
}

} // namespace chronon3d::content::text
