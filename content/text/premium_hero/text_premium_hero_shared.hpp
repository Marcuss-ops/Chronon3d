#pragma once

#include "../helpers/text_helpers.hpp"

#include <chronon3d/scene/builders/scene_builder.hpp>

#include <string>
#include <utility>

namespace chronon3d::content::text::premium {

inline TextShadow shadow(Vec2 offset, f32 blur, f32 opacity, Color color) {
    return TextShadow{
        .enabled = true,
        .offset = offset,
        .blur = blur,
        .opacity = opacity,
        .color = color,
    };
}

inline TextParams hero_text(
    std::string text,
    Vec2 size,
    Vec3 pos,
    f32 font_size,
    std::string font_path,
    std::string font_family,
    Color fill,
    Fill fill_style,
    Color stroke_color,
    f32 stroke_width,
    f32 tracking,
    VerticalAlign vertical_align = VerticalAlign::Middle,
    ShadowStyle shadow_style = ShadowStyle{},
    TextWrap wrap = TextWrap::None
) {
    TextParams p;
    p.text = std::move(text);
    p.size = size;
    p.pos = pos;
    p.font_path = std::move(font_path);
    p.font_family = std::move(font_family);
    p.font_weight = 900;
    p.font_style = "normal";
    p.font_size = font_size;
    p.color = fill;
    p.align = TextAlign::Center;
    p.vertical_align = vertical_align;
    p.line_height = 0.95f;
    p.tracking = tracking;
    p.wrap = wrap;
    p.paint.fill = fill;
    p.paint.fill_style = std::move(fill_style);
    p.paint.stroke_enabled = true;
    p.paint.stroke_color = stroke_color;
    p.paint.stroke_width = stroke_width;
    p.shadows = make_text_shadows(shadow_style);
    return p;
}

inline TextParams subtitle_text(
    std::string text,
    Vec2 size,
    Vec3 pos,
    f32 font_size,
    Color color,
    f32 tracking = 1.2f,
    ShadowStyle shadow_style = ShadowStyle{},
    TextWrap wrap = TextWrap::None
) {
    TextParams p;
    p.text = std::move(text);
    p.size = size;
    p.pos = pos;
    p.font_path = "assets/fonts/Inter-Regular.ttf";
    p.font_family = "Inter";
    p.font_weight = 400;
    p.font_style = "normal";
    p.font_size = font_size;
    p.color = color;
    p.align = TextAlign::Center;
    p.vertical_align = VerticalAlign::Middle;
    p.line_height = 1.1f;
    p.tracking = tracking;
    p.wrap = wrap;
    p.shadows = make_text_shadows(shadow_style);
    return p;
}

inline ShadowStyle shadow_style(
    Color contact_color,
    Color ambient_color,
    Vec2 contact_offset = {0.0f, 6.0f},
    f32 contact_blur = 14.0f,
    f32 contact_opacity = 0.28f,
    Vec2 ambient_offset = {0.0f, 40.0f},
    f32 ambient_blur = 120.0f,
    f32 ambient_opacity = 0.08f
) {
    ShadowStyle style;
    style.contact = shadow(contact_offset, contact_blur, contact_opacity, contact_color);
    style.ambient = shadow(ambient_offset, ambient_blur, ambient_opacity, ambient_color);
    return style;
}

inline void add_soft_orb(SceneBuilder& s, const std::string& layer, Vec3 pos, f32 radius, Color color, f32 glow_radius) {
    s.layer(layer, [=](LayerBuilder& l) {
        l.position(pos);
        l.glow(glow_radius, 0.85f, color);
        l.circle("orb", {
            .radius = radius,
            .color = color,
            .pos = {0.0f, 0.0f, 0.0f},
        });
    });
}

inline void add_ambient_blob(SceneBuilder& s, const std::string& layer, Vec3 pos, f32 radius, Color color, f32 blur_radius, f32 opacity = 1.0f) {
    s.layer(layer, [=](LayerBuilder& l) {
        l.position(pos);
        l.opacity(opacity);
        l.blur(blur_radius);
        l.glow(radius * 0.45f, 0.55f, color);
        l.circle("blob", {
            .radius = radius,
            .color = color,
            .pos = {0.0f, 0.0f, 0.0f},
        });
    });
}

inline void add_scatter(SceneBuilder& s, Color color) {
    s.layer("scatter", [=](LayerBuilder& l) {
        l.circle("s1", {.radius = 7.0f, .color = color, .pos = {-420.0f, -120.0f, 0.0f}});
        l.circle("s2", {.radius = 4.5f, .color = color, .pos = {430.0f, -95.0f, 0.0f}});
        l.circle("s3", {.radius = 3.2f, .color = color, .pos = {380.0f, 115.0f, 0.0f}});
    });
}

inline void add_premium_grid(SceneBuilder& s, Color grid_color, f32 spacing = 96.0f) {
    s.layer("grid", [=](LayerBuilder& l) {
        l.opacity(0.13f);
        l.grid_background("grid_bg", {
            .size = {W, H},
            .bg_color = {0.0f, 0.0f, 0.0f, 0.0f},
            .grid_color = grid_color,
            .spacing = spacing,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true,
        });
    });
}

inline void add_masterclass_badge(SceneBuilder& s) {
    s.layer("top_icons", [](LayerBuilder& l) {
        l.position({0.0f, -416.0f, 0.0f});
        l.rounded_rect("ae", {
            .size = {66.0f, 66.0f},
            .radius = 18.0f,
            .color = {0.97f, 0.98f, 1.0f, 0.95f},
            .pos = {-100.0f, 0.0f, 0.0f},
            .fill = Fill::linear(
                {0.0f, 0.0f},
                {0.0f, 1.0f},
                {
                    {0.0f, {0.98f, 0.99f, 1.0f, 1.0f}},
                    {1.0f, {0.60f, 0.84f, 1.0f, 0.96f}},
                }
            )
        });
        l.text("ae_text", {
            .text = "Ae",
            .size = {66.0f, 66.0f},
            .pos = {-100.0f, 0.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .font_family = "Inter",
            .font_weight = 900,
            .font_style = "normal",
            .font_size = 26.0f,
            .color = {0.10f, 0.12f, 0.18f, 1.0f},
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
        });
        l.circle("pin", {
            .radius = 24.0f,
            .color = {1.0f, 0.45f, 0.62f, 0.96f},
            .pos = {820.0f, -4.0f, 0.0f},
        });
        l.circle("pin_halo", {
            .radius = 34.0f,
            .color = {1.0f, 0.45f, 0.62f, 0.14f},
            .pos = {820.0f, -4.0f, 0.0f},
        });
        l.glow(22.0f, 1.2f, {1.0f, 0.45f, 0.62f, 1.0f});
    });
}

} // namespace chronon3d::content::text::premium
