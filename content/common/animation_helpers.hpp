#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <string>

namespace chronon3d::content::animation_helpers {

// ── Shared constants ───────────────────────────────────────────────────────
inline constexpr Color SHADOW_COLOR = {0.0f, 0.0f, 0.0f, 0.15f};
inline constexpr Color TEXT_COLOR   = {0.92f, 0.93f, 0.97f, 1.0f};
inline constexpr const char* FONT_REGULAR = "assets/fonts/Poppins-Regular.ttf";
inline constexpr f32   BOX_W = 1200.0f;
inline constexpr f32   BOX_H = 240.0f;

// ── add_black_background ───────────────────────────────────────────────────
// Full-screen black background layer used by all animation compositions.
inline void add_black_background(SceneBuilder& s) {
    s.layer("_bg", [](LayerBuilder& l) {
        l.fullscreen_rect("bg", Color{0.0f, 0.0f, 0.0f, 1.0f});
    });
}

// ── make_text ──────────────────────────────────────────────────────────────
// Standard centered text: 1200×240 box, Poppins-Bold, consistent tracking &
// drop-shadow-ready color.  Designed for AnimFadeIn, AnimSlide, AnimScale.
inline TextSpec make_text(const std::string& text, f32 font_size = 64.0f) {
    return TextSpec{
        .content = {.value = text},
        .font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_size = font_size},
        .layout = {.box = {BOX_W, BOX_H}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .line_height = 0.95f, .tracking = 3.0f},
        .appearance = {.color = TEXT_COLOR},
        .position = {0.0f, 0.0f, 0.0f},
    };
}

} // namespace chronon3d::content::animation_helpers
