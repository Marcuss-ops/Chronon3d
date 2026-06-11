#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include "content/common/background_helpers.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace chronon3d::content::minimalist {

// ═════════════════════════════════════════════════════════════════════════════
// Minimalist Theme — shared constants & helpers
// ═════════════════════════════════════════════════════════════════════════════

// ── Colors ─────────────────────────────────────────────────────────────────
inline constexpr Color TEXT_COLOR       = {0.94f, 0.94f, 0.94f, 1.0f};
inline constexpr Color TEXT_COLOR_WHITE = {1.0f,  1.0f,  1.0f,  1.0f};
inline constexpr Color BACKDROP_COLOR   = {0.0f,  0.0f,  0.0f,  0.22f};
inline constexpr Color BORDER_COLOR     = {0.25f, 0.27f, 0.31f, 0.8f};
inline constexpr Color IMAGE_BACKDROP   = {0.0f,  0.0f,  0.0f,  0.35f};

// ── Typography ─────────────────────────────────────────────────────────────
inline constexpr const char* FONT_PATH = "assets/fonts/Poppins-Bold.ttf";
inline constexpr f32 FONT_SIZE         = 58.0f;
inline constexpr f32 MIN_FONT_SIZE     = 42.0f;
inline constexpr f32 MAX_FONT_SIZE   = 64.0f;
inline constexpr f32 LINE_HEIGHT         = 1.10f;

// ── Dimensions ─────────────────────────────────────────────────────────────
inline constexpr Vec2 BACKDROP_SIZE      = {1580.0f, 380.0f};
inline constexpr Vec2 BACKDROP_SIZE_SMALL= {1580.0f, 330.0f};
inline constexpr Vec2 TEXT_SIZE          = {1450.0f, 340.0f};
inline constexpr Vec2 TEXT_SIZE_SMALL    = {1450.0f, 280.0f};
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

// ── Common Text Parameters ─────────────────────────────────────────────────
inline TextParams make_text_params(std::string text, Vec2 size = TEXT_SIZE) {
    return TextParams{
        .text            = std::move(text),
        .size            = size,
        .pos             = {0.0f, 0.0f, 0.0f},
        .font_path       = FONT_PATH,
        .font_size       = FONT_SIZE,
        .color           = TEXT_COLOR,
        .align           = TextAlign::Left,
        .vertical_align  = VerticalAlign::Middle,
        .line_height     = LINE_HEIGHT,
        .tracking        = 0.0f,
        .auto_fit        = false,
        .max_lines       = 3,
        .min_font_size   = MIN_FONT_SIZE,
        .max_font_size   = MAX_FONT_SIZE,
        .overflow        = TextOverflow::Clip,
        .wrap            = TextWrap::None,
    };
}

// ── Reveal Text Helper ─────────────────────────────────────────────────────
// Reveals a substring at chars_per_frame speed.  Returns a single space if
// nothing is visible yet so the text layer does not collapse to zero size.
inline std::string reveal_text_by_frame(const std::string& full_text, Frame frame, f32 chars_per_frame = 1.5f) {
    size_t visible = std::min(full_text.size(), static_cast<size_t>(static_cast<f32>(frame) * chars_per_frame));
    if (visible == 0) return " ";
    return full_text.substr(0, visible);
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
