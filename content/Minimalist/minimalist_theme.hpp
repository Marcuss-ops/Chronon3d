#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include "content/common/background_helpers.hpp"

namespace chronon3d::content::minimalist {

// ═════════════════════════════════════════════════════════════════════════════
// Minimalist Theme — shared constants & helpers
// ═════════════════════════════════════════════════════════════════════════════

// ── Colors ─────────────────────────────────────────────────────────────────
inline constexpr Color BACKDROP_COLOR   = {0.0f,  0.0f,  0.0f,  0.22f};
inline constexpr Color BORDER_COLOR     = {0.25f, 0.27f, 0.31f, 0.8f};
inline constexpr Color IMAGE_BACKDROP   = {0.0f,  0.0f,  0.0f,  0.35f};

// ── Dimensions ─────────────────────────────────────────────────────────────
inline constexpr Vec2 BACKDROP_SIZE      = {1580.0f, 380.0f};
inline constexpr f32  BACKDROP_RADIUS    = 28.0f;

// ── Image defaults ─────────────────────────────────────────────────────────
inline constexpr f32  IMAGE_RADIUS        = 8.0f;
inline constexpr const char* IMAGE_PATH = "assets/images/minimalist_landscape.png";
inline constexpr Vec2 IMAGE_SIZE        = {800.0f, 450.0f};

// ── Background ───────────────────────────────────────────────────────────────
inline void add_minimalist_background(SceneBuilder& s) {
    using chronon3d::content::backgrounds::add_common_background;
    add_common_background(s, chronon3d::content::backgrounds::BackgroundStyles::Minimalist());
}

// ── Text Backdrop ──────────────────────────────────────────────────────────
inline void add_text_backdrop(LayerBuilder& l, Vec2 size = BACKDROP_SIZE) {
    l.rounded_rect("text_backdrop", {
        .size   = size,
        .radius = BACKDROP_RADIUS,
        .color  = BACKDROP_COLOR,
        .pos    = {0.0f, 0.0f, 0.0f},
    });
}

// ── Image Border / Frame ─────────────────────────────────────────────────────
inline void add_image_border(LayerBuilder& l, Vec2 size = IMAGE_SIZE) {
    l.rounded_rect("image_backdrop", {
        .size   = size + Vec2{24.0f, 24.0f},
        .radius = 16.0f,
        .color  = IMAGE_BACKDROP,
        .pos    = {0.0f, 0.0f, 0.0f},
    });
    l.rounded_rect("image_border", {
        .size   = size + Vec2{2.0f, 2.0f},
        .radius = 10.0f,
        .color  = BORDER_COLOR,
        .pos    = {0.0f, 0.0f, 0.0f},
    });
}

} // namespace chronon3d::content::minimalist
