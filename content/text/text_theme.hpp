#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include "content/common/background_helpers.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace chronon3d::content::text {

// ═════════════════════════════════════════════════════════════════════════════
// Text Theme — shared constants & helpers for text/simple-grid compositions
// ═════════════════════════════════════════════════════════════════════════════

// ── Colors ─────────────────────────────────────────────────────────────────
inline constexpr Color TEXT_COLOR_BLUE    = {0.25f, 0.58f, 1.0f, 1.0f};
inline constexpr Color TEXT_COLOR_WHITE   = {0.9f,  0.92f, 0.98f, 1.0f};
inline constexpr Color SUBTITLE_COLOR     = {0.78f, 0.82f, 0.9f, 1.0f};
inline constexpr Color CAPTION_COLOR      = {0.6f,  0.65f, 0.8f, 1.0f};
inline constexpr Color ATTR_COLOR         = {0.4f,  0.45f, 0.6f, 1.0f};
inline constexpr Color DIVIDER_COLOR      = {0.22f, 0.28f, 0.38f, 1.0f};
inline constexpr Color TITLE_COLOR        = {0.7f,  0.75f, 0.9f, 1.0f};
inline constexpr Color BADGE_TEXT_COLOR   = {1.0f,  1.0f,  1.0f, 1.0f};
inline constexpr Color IMAGE_SHADOW_COLOR = {0.0f,  0.0f,  0.0f, 0.35f};
inline constexpr Color IMAGE_BORDER_COLOR = {0.25f, 0.27f, 0.31f, 0.7f};

// ── Fresh palette (for new compositions) ─────────────────────────────────
inline constexpr Color FRESH_GLOW_BLUE  = {0.18f, 0.55f, 1.0f, 0.75f};
inline constexpr Color FRESH_GLOW_GOLD  = {1.0f, 0.72f, 0.22f, 0.70f};
inline constexpr Color FRESH_GLOW_CYAN  = {0.22f, 0.95f, 1.0f, 0.65f};
inline constexpr Color FRESH_GLOW_PINK  = {1.0f, 0.35f, 0.65f, 0.70f};
inline constexpr Color FRESH_TEXT_WHITE = {0.95f, 0.96f, 0.98f, 1.0f};
inline constexpr Color FRESH_TEXT_MUTED = {0.55f, 0.60f, 0.72f, 1.0f};

// ── Typography ─────────────────────────────────────────────────────────────
inline constexpr const char* TEXT_FONT_PATH = "assets/fonts/Poppins-Bold.ttf";

// ── Background ─────────────────────────────────────────────────────────────
inline void add_text_background(SceneBuilder& s) {
    using chronon3d::content::backgrounds::add_common_background;
    add_common_background(s, chronon3d::content::backgrounds::BackgroundStyles::TextTypewriter());
}

// ── Fade-in opacity helper ─────────────────────────────────────────────────
inline f32 fade_in(Frame frame, f32 start, f32 duration) {
    f32 d = std::max(duration, 1.0f);
    f32 t = std::clamp((static_cast<f32>(frame) - start) / d, 0.0f, 1.0f);
    return interpolate(t, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
}

// ── Image card helper (shadow + border + image) ────────────────────────────
inline void add_image_card(LayerBuilder& l, const char* path, Vec2 size, f32 radius) {
    l.rounded_rect("shadow", {
        .size   = size + Vec2{24.0f, 24.0f},
        .radius = radius + 4.0f,
        .color  = IMAGE_SHADOW_COLOR,
        .pos    = {0.0f, 6.0f, 0.0f},
    });
    l.rounded_rect("border", {
        .size   = size + Vec2{2.0f, 2.0f},
        .radius = radius + 2.0f,
        .color  = IMAGE_BORDER_COLOR,
    });
    l.image("img", {
        .path   = path,
        .size   = size,
        .radius = radius,
    });
}

// ── Badge helper (rounded rect + centered text) ────────────────────────────
inline void add_badge(LayerBuilder& l, const std::string& badge_text, Vec2 size,
                      f32 radius, Color bg_color, f32 font_size) {
    l.rounded_rect("bg", {.size = size, .radius = radius, .color = bg_color});
    l.text("label", {
        .text            = badge_text,
        .size            = size,
        .font_path       = TEXT_FONT_PATH,
        .font_size       = font_size,
        .color           = BADGE_TEXT_COLOR,
        .align           = TextAlign::Center,
        .vertical_align  = VerticalAlign::Middle,
        .tracking        = 8.0f,
    });
}

} // namespace chronon3d::content::text
