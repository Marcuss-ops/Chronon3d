#pragma once

// ── Shared Helpers for reference_2_5d_suite ──────────────────────────────────
// Extracted from the two split files (_cards.cpp and _scenes.cpp) to eliminate
// the duplicated anonymous-namespace helpers that the previous split introduced.
//
// Include this header from both _cards.cpp and _scenes.cpp.

#include "glow_test_common.hpp"
#include <chronon3d/effects/effect_params.hpp>

namespace chronon3d::content::effects {
namespace ref_2_5d_helpers {

inline constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

inline void add_header(SceneBuilder& s, const std::string& id, const std::string& title, const std::string& subtitle) {
    const f32 title_font_size = title.size() > 24 ? 20.0f : (title.size() > 20 ? 22.0f : 24.0f);
    const f32 subtitle_font_size = title.size() > 24 ? 13.0f : 14.0f;
    s.layer("header_" + id, [=](LayerBuilder& l) {
        l.position({-kHW + 42.0f, -kHH + 42.0f, 0.0f});
        l.rounded_rect("badge", {.size = {52.0f, 52.0f}, .radius = 12.0f, .color = {0.34f, 0.38f, 0.78f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::linear({0.0f, 0.0f}, {0.0f, 1.0f}, {{0.0f, {0.55f, 0.34f, 0.98f, 1.0f}}, {1.0f, {0.18f, 0.14f, 0.62f, 1.0f}}})});
        l.text("badge_txt", TextSpec{.content = {.value = id}, .placement = {TextPlacementKind::Absolute, {0.0f, 0.0f}}, .font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Inter", .font_weight = 900, .font_style = "normal", .font_size = 24.0f}, .layout = {.box = {52.0f, 52.0f}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle}, .appearance = {.color = kWhite}});
        l.text("title", TextSpec{.content = {.value = title}, .placement = {TextPlacementKind::Absolute, {84.0f, -2.0f}}, .font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Inter", .font_weight = 900, .font_style = "normal", .font_size = title_font_size}, .layout = {.box = {1060.0f, 36.0f}, .align = TextAlign::Left, .vertical_align = VerticalAlign::Middle}, .appearance = {.color = kWhite}});
        l.text("subtitle", TextSpec{.content = {.value = subtitle}, .placement = {TextPlacementKind::Absolute, {84.0f, 28.0f}}, .font = {.font_path = "assets/fonts/Poppins-Regular.ttf", .font_family = "Inter", .font_weight = 400, .font_style = "normal", .font_size = subtitle_font_size}, .layout = {.box = {1060.0f, 24.0f}, .align = TextAlign::Left, .vertical_align = VerticalAlign::Middle}, .appearance = {.color = {0.78f, 0.82f, 0.92f, 1.0f}}});
    });
}

inline void add_neon_floor(SceneBuilder& s, Color grid, f32 spacing = 80.0f) {
    s.layer("floor_grid", [=](LayerBuilder& l) { l.position({0.0f, 250.0f, 0.0f}); l.opacity(0.30f); l.grid_background("grid", {.size = {(f32)kW, (f32)kH * 0.7f}, .bg_color = {0.0f, 0.0f, 0.0f, 0.0f}, .grid_color = grid, .spacing = spacing, .minor_thickness = 1.0f, .major_thickness = 2.0f, .major_every = 4, .centered = true}); });
}

inline void apply_cinematic_lighting(SceneBuilder& s, Color ambient_color, f32 ambient_intensity, Vec3 dir, Color dir_color, f32 diffuse) {
    s.ambient_light(ambient_color, ambient_intensity);
    s.directional_light(dir, dir_color, diffuse);
}

inline Material2_5D make_depth_material(bool casts_shadows = true, bool accepts_shadows = true, f32 diffuse = 0.82f, f32 specular = 0.18f, f32 shininess = 48.0f, f32 ambient = 0.72f) {
    return Material2_5D{.accepts_lights = true, .casts_shadows = casts_shadows, .accepts_shadows = accepts_shadows, .diffuse = diffuse, .specular = specular, .shininess = shininess, .ambient_multiplier = ambient};
}

inline void apply_depth_material(LayerBuilder& l, bool casts_shadows = true, bool accepts_shadows = true, f32 diffuse = 0.82f, f32 specular = 0.18f, f32 shininess = 48.0f, f32 ambient = 0.72f) {
    l.accepts_lights(true).casts_shadows(casts_shadows).accepts_shadows(accepts_shadows).material(make_depth_material(casts_shadows, accepts_shadows, diffuse, specular, shininess, ambient));
}

} // namespace ref_2_5d_helpers
} // namespace chronon3d::content::effects
