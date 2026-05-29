#pragma once

#include <chronon3d/scene/shape.hpp>
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

} // namespace chronon3d::presets::text
