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
};

struct StyleLayer {
    TextDef      def;
    f32          y_pos{0.0f};
    Vec2         size{W * 0.7f, 160.0f};
};

void apply_text(LayerBuilder& l, const std::string& name, const TextDef& d,
                Vec2 sz = {W * 0.7f, 160.0f}, Vec3 pos = {0.0f, 0.0f, 0.0f});

} // namespace chronon3d::content::text
