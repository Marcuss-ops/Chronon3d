#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <string>
#include <utility>

namespace chronon3d::presets {

struct TextSpec {
    std::string text;
    f32 size{32.0f};
    f32 tracking{0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlign align{TextAlign::Center};
    std::string font_family{"Inter"};
    f32 line_height{1.2f};
    i32 font_weight{800};

    TextSpec() = default;
    explicit TextSpec(std::string t) : text(std::move(t)) {}

    TextSpec& set_text(std::string t) {
        text = std::move(t);
        return *this;
    }

    TextSpec& set_font(f32 s, f32 t = 0.0f) {
        size = s;
        tracking = t;
        return *this;
    }

    TextSpec& set_color(Color c) {
        color = c;
        return *this;
    }

    TextSpec& set_align(TextAlign a) {
        align = a;
        return *this;
    }

    TextSpec& set_font_family(std::string family) {
        font_family = std::move(family);
        return *this;
    }

    TextSpec& set_line_height(f32 lh) {
        line_height = lh;
        return *this;
    }

    TextSpec& set_font_weight(i32 weight) {
        font_weight = weight;
        return *this;
    }

    void draw(LayerBuilder& l, const std::string& id, Vec2 box = {1344.0f, 160.0f}, Vec3 pos = {0.0f, 0.0f, 0.0f}) const {
        l.text(id, {
            .text = text,
            .size = box,
            .pos = pos,
            .font_family = font_family,
            .font_weight = font_weight,
            .font_size = size,
            .color = color,
            .align = align,
            .line_height = line_height,
            .tracking = tracking
        });
    }
};

} // namespace chronon3d::presets
