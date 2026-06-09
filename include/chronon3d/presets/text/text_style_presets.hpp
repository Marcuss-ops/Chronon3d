#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/math/color.hpp>
#include <vector>

namespace chronon3d::presets::text {

inline TextStyle headline() {
    TextStyle style;
    style.font_family = "Inter";
    style.font_weight = 900;
    style.size = 82.0f;
    style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    style.align = TextAlign::Center;
    style.vertical_align = VerticalAlign::Middle;
    style.paint.stroke_enabled = true;
    style.paint.stroke_width = 3.0f;
    style.paint.stroke_color = Color{0.0f, 0.0f, 0.0f, 1.0f};
    
    TextShadow shadow;
    shadow.enabled = true;
    shadow.offset = {0, 10};
    shadow.color = Color{0, 0, 0, 0.5f};
    shadow.blur = 16.0f;
    style.shadows.push_back(shadow);
    return style;
}

inline TextStyle subtitle() {
    TextStyle style;
    style.font_family = "Inter";
    style.font_weight = 600;
    style.size = 48.0f;
    style.color = Color{0.9f, 0.9f, 0.9f, 1.0f};
    style.align = TextAlign::Center;
    style.vertical_align = VerticalAlign::Middle;
    style.box_style.enabled = true;
    style.box_style.background = Color{0.0f, 0.0f, 0.0f, 0.65f};
    style.box_style.padding = {16.0f, 8.0f};
    style.box_style.radius = 8.0f;
    return style;
}

inline TextStyle lower_third() {
    TextStyle style;
    style.font_family = "Inter";
    style.font_weight = 700;
    style.size = 36.0f;
    style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    style.align = TextAlign::Left;
    style.vertical_align = VerticalAlign::Top;
    return style;
}

inline TextStyle quote() {
    TextStyle style;
    style.font_family = "Playfair Display";
    style.font_style = "italic";
    style.font_weight = 400;
    style.size = 54.0f;
    style.color = Color{0.85f, 0.85f, 0.85f, 1.0f};
    style.align = TextAlign::Center;
    style.vertical_align = VerticalAlign::Middle;
    return style;
}

inline TextStyle breaking_news() {
    TextStyle style;
    style.font_family = "Inter";
    style.font_weight = 900;
    style.size = 72.0f;
    style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    style.align = TextAlign::Center;
    style.vertical_align = VerticalAlign::Middle;
    style.box_style.enabled = true;
    style.box_style.background = Color{0.85f, 0.05f, 0.05f, 1.0f}; // Red banner
    style.box_style.padding = {24.0f, 12.0f};
    style.box_style.radius = 4.0f;
    return style;
}

inline TextStyle luxury_gold() {
    TextStyle style;
    style.font_family = "Cinzel";
    style.font_weight = 700;
    style.size = 64.0f;
    style.color = Color{0.86f, 0.72f, 0.43f, 1.0f}; // Gold color
    style.align = TextAlign::Center;
    style.vertical_align = VerticalAlign::Middle;
    
    TextShadow shadow;
    shadow.enabled = true;
    shadow.offset = {0, 6};
    shadow.color = Color{0, 0, 0, 0.4f};
    shadow.blur = 12.0f;
    style.shadows.push_back(shadow);
    return style;
}

inline TextStyle neon() {
    TextStyle style;
    style.font_family = "Inter";
    style.font_weight = 800;
    style.size = 64.0f;
    style.color = Color{0.0f, 1.0f, 0.8f, 1.0f}; // Cyan
    style.align = TextAlign::Center;
    style.vertical_align = VerticalAlign::Middle;
    
    // Neon glow effect using multiple shadows
    TextShadow s1{ .enabled=true, .offset={0,0}, .blur=8.0f, .opacity=1.0f, .color=Color{0.0f, 1.0f, 0.8f, 0.8f} };
    TextShadow s2{ .enabled=true, .offset={0,0}, .blur=20.0f, .opacity=1.0f, .color=Color{0.0f, 1.0f, 0.8f, 0.5f} };
    TextShadow s3{ .enabled=true, .offset={0,0}, .blur=40.0f, .opacity=1.0f, .color=Color{0.0f, 1.0f, 0.8f, 0.3f} };
    style.shadows.push_back(s1);
    style.shadows.push_back(s2);
    style.shadows.push_back(s3);
    return style;
}

inline TextStyle premium_hero_title() {
    TextStyle style;
    style.font_family = "Inter";
    style.font_weight = 900;
    style.size = 108.0f;
    style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    style.align = TextAlign::Center;
    style.vertical_align = VerticalAlign::Middle;
    style.line_height = 0.95f;
    style.tracking = -1.0f;
    style.paint.fill = Color{1.0f, 1.0f, 1.0f, 1.0f};
    style.paint.fill_style = Fill::linear(
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {
            {0.0f, Color{1.0f, 0.94f, 0.52f, 1.0f}},
            {0.52f, Color{1.0f, 0.30f, 0.82f, 1.0f}},
            {1.0f, Color{0.78f, 0.18f, 0.96f, 1.0f}},
        }
    );
    style.paint.stroke_enabled = true;
    style.paint.stroke_color = Color{0.0f, 0.02f, 0.12f, 0.92f};
    style.paint.stroke_width = 3.0f;
    style.material = TextMaterial::premium();
    style.material.top_color = {1.0f, 0.96f, 0.76f, 1.0f};
    style.material.bottom_color = {0.95f, 0.40f, 0.88f, 1.0f};
    style.material.bevel_px = 2.2f;
    style.material.bevel_highlight_opacity = 0.55f;
    style.material.bevel_shadow_opacity = 0.34f;
    style.material.top_highlight_opacity = 0.28f;
    style.material.bottom_shade_opacity = 0.18f;
    style.material.emissive = 1.10f;
    style.material.use_material_glow = true;
    style.material.glow_radius = 24.0f;
    style.material.glow_intensity = 0.92f;
    style.material.glow_color = {0.55f, 0.72f, 1.0f, 0.75f};
    style.material.use_material_shadow = true;
    style.material.shadow_offset = {0.0f, 10.0f};
    style.material.shadow_blur = 18.0f;
    style.material.shadow_opacity = 0.50f;
    style.material.shadow_color = {0.0f, 0.0f, 0.0f, 1.0f};
    style.shadows.push_back(TextShadow{
        .enabled = true,
        .offset = {0.0f, 12.0f},
        .blur = 28.0f,
        .opacity = 0.28f,
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    });
    style.shadows.push_back(TextShadow{
        .enabled = true,
        .offset = {0.0f, 0.0f},
        .blur = 20.0f,
        .opacity = 0.55f,
        .color = {0.40f, 0.20f, 1.0f, 1.0f},
    });
    return style;
}

inline TextStyle premium_subtitle() {
    TextStyle style;
    style.font_family = "Inter";
    style.font_weight = 700;
    style.size = 28.0f;
    style.color = Color{0.92f, 0.95f, 1.0f, 1.0f};
    style.align = TextAlign::Center;
    style.vertical_align = VerticalAlign::Middle;
    style.line_height = 1.0f;
    style.tracking = 1.2f;
    style.paint.fill = Color{0.92f, 0.95f, 1.0f, 1.0f};
    style.paint.fill_style = Fill::linear(
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {
            {0.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}},
            {1.0f, Color{0.70f, 0.88f, 1.0f, 1.0f}},
        }
    );
    style.paint.stroke_enabled = true;
    style.paint.stroke_color = Color{0.0f, 0.05f, 0.16f, 0.55f};
    style.paint.stroke_width = 1.2f;
    style.material = TextMaterial::glass();
    style.material.top_color = {1.0f, 1.0f, 1.0f, 0.90f};
    style.material.bottom_color = {0.80f, 0.90f, 1.0f, 0.82f};
    style.material.bevel_px = 0.8f;
    style.material.top_highlight_opacity = 0.22f;
    style.material.bottom_shade_opacity = 0.10f;
    style.material.glow_radius = 10.0f;
    style.material.glow_intensity = 0.45f;
    style.material.glow_color = {0.62f, 0.82f, 1.0f, 0.45f};
    style.shadows.push_back(TextShadow{
        .enabled = true,
        .offset = {0.0f, 4.0f},
        .blur = 10.0f,
        .opacity = 0.35f,
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    });
    return style;
}

inline TextStyle premium_cta() {
    TextStyle style;
    style.font_family = "Inter";
    style.font_weight = 900;
    style.size = 40.0f;
    style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    style.align = TextAlign::Center;
    style.vertical_align = VerticalAlign::Middle;
    style.line_height = 1.0f;
    style.tracking = 4.0f;
    style.paint.fill = Color{1.0f, 1.0f, 1.0f, 1.0f};
    style.paint.fill_style = Fill::linear(
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {
            {0.0f, Color{1.0f, 0.98f, 0.90f, 1.0f}},
            {1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}},
        }
    );
    style.paint.stroke_enabled = true;
    style.paint.stroke_color = Color{0.10f, 0.04f, 0.12f, 0.55f};
    style.paint.stroke_width = 1.0f;
    style.material = TextMaterial::glass();
    style.material.top_color = {1.0f, 1.0f, 1.0f, 0.98f};
    style.material.bottom_color = {0.95f, 0.97f, 1.0f, 0.90f};
    style.material.bevel_px = 0.9f;
    style.material.use_material_glow = true;
    style.material.glow_radius = 8.0f;
    style.material.glow_intensity = 0.38f;
    style.material.glow_color = {1.0f, 0.58f, 0.18f, 0.35f};
    style.shadows.push_back(TextShadow{
        .enabled = true,
        .offset = {0.0f, 2.0f},
        .blur = 8.0f,
        .opacity = 0.25f,
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    });
    return style;
}

} // namespace chronon3d::presets::text
