#pragma once

// ── Shared Text Helpers ─────────────────────────────────────────────────────
//
// Canonical text-building helpers used by ALL composition families.
// Three small functions that cover 95% of content text needs:
//
//   1. centered_text()   — one-liner centered text with auto-fit
//   2. typewriter_text() — character-by-character reveal
//   3. glow_text()       — text params tuned for glow layers
//
// Each returns a plain TextParams that can be passed directly to
// LayerBuilder::text().  Callers compose with the existing glow / shadow APIs:
//
//   #include "content/text/text_helpers.hpp"
//
//   SceneBuilder s(ctx);
//   s.layer("hero", [](LayerBuilder& l) {
//       l.pin_to(Anchor::Center);
//       content::text::glow::apply_ae_glow(l);  // from content/common/text_helpers.hpp
//       l.text("t", content::text::centered_text({
//           .text      = "TITLE",
//           .font_size = 100,
//           .tracking  = 6,
//       }));
//   });
//
// Namespace: chronon3d::content::text
// Same namespace as content/text/text_theme.hpp — constants are shared.
//

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <string>
#include <algorithm>
#include <utility>

namespace chronon3d::content::text {

// ═════════════════════════════════════════════════════════════════════════════
// CenterTextOptions — unified options struct for all text helpers
// ═════════════════════════════════════════════════════════════════════════════
//
// Every helper accepts this by value (small, fits in registers) and consumes
// the `text` member via std::move.
//
struct CenterTextOptions {
    std::string text;                     // text content (consumed)
    Vec2  box{1200.0f, 240.0f};           // logical text box
    Vec3  pos{0.0f, 0.0f, 0.0f};          // position (anchor is always Center)
    std::string font_path{"assets/fonts/Poppins-Bold.ttf"};
    std::string font_family{"Poppins"};
    int   font_weight{700};
    std::string font_style{"normal"};
    f32   font_size{96.0f};               // target font size
    f32   tracking{0.0f};                 // extra px per glyph
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    int   max_lines{1};                   // 0 = unlimited
    bool  auto_fit{false};                 // shrink to fit box (off for predictable sizing)
    f32   line_height{0.95f};             // multiplier of font-size
    f32   min_font_size{12.0f};           // floor for auto-fit
    f32   max_font_size{160.0f};          // ceiling for auto-fit
};

// ═════════════════════════════════════════════════════════════════════════════
// 1. centered_text
// ═════════════════════════════════════════════════════════════════════════════
//
// Returns TextParams for centered, middle-aligned text inside `box`.
// Anchor is always TextAnchor::Center so pos maps to box center.
// Uses PixelInk centering mode for optical centering (accounts for glyph
// asymmetries that layout-box centering misses).
//
// Example:
//   l.text("t", centered_text({.text = "TITLE", .font_size = 96, .tracking = 6}));
//
inline TextParams centered_text(CenterTextOptions o) {
    return TextParams{
        .text            = std::move(o.text),
        .size            = o.box,
        .pos             = o.pos,
        .font_path       = std::move(o.font_path),
        .font_family     = std::move(o.font_family),
        .font_weight     = o.font_weight,
        .font_style      = std::move(o.font_style),
        .font_size       = o.font_size,
        .color           = o.color,
        .anchor          = TextAnchor::Center,
        .centering_mode  = TextCenteringMode::PixelInk,
        .align           = TextAlign::Center,
        .vertical_align  = VerticalAlign::Middle,
        .line_height     = o.line_height,
        .tracking        = o.tracking,
        .auto_fit        = o.auto_fit,
        .max_lines       = o.max_lines,
        .min_font_size   = o.min_font_size,
        .max_font_size   = o.max_font_size,
        .overflow        = TextOverflow::Clip,
        .wrap            = TextWrap::Word,
    };
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. typewriter_text
// ═════════════════════════════════════════════════════════════════════════════
//
// Like centered_text but reveals `text` character-by-character based on
// the current frame.  Returns a single space (" ") for frame 0 so the
// text layer never collapses to zero size.
//
// Example:
//   l.text("t", typewriter_text({.text = "HELLO", .font_size = 72}, ctx.frame(), 2.0f));
//
inline TextParams typewriter_text(CenterTextOptions o,
                                  Frame frame,
                                  f32 chars_per_frame = 1.5f) {
    const size_t visible = std::min(
        o.text.size(),
        static_cast<size_t>(static_cast<f32>(frame) * chars_per_frame));
    std::string revealed = (visible == 0)
        ? std::string(" ")
        : o.text.substr(0, visible);

    // Build centered params with the revealed substring
    return TextParams{
        .text            = std::move(revealed),
        .size            = o.box,
        .pos             = o.pos,
        .font_path       = std::move(o.font_path),
        .font_family     = std::move(o.font_family),
        .font_weight     = o.font_weight,
        .font_style      = std::move(o.font_style),
        .font_size       = o.font_size,
        .color           = o.color,
        .anchor          = TextAnchor::Center,
        .centering_mode  = TextCenteringMode::PixelInk,
        .align           = TextAlign::Center,
        .vertical_align  = VerticalAlign::Middle,
        .line_height     = o.line_height,
        .tracking        = o.tracking,
        .auto_fit        = o.auto_fit,
        .max_lines       = o.max_lines,
        .min_font_size   = o.min_font_size,
        .max_font_size   = o.max_font_size,
        .overflow        = TextOverflow::Clip,
        .wrap            = TextWrap::Word,
    };
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. glow_text
// ═════════════════════════════════════════════════════════════════════════════
//
// Like centered_text but tuned for use with glow effects.  Currently
// identical to centered_text — the glow effect itself is applied via
// LayerBuilder::glow() or glow::apply_ae_glow().  This helper exists as
// a semantic marker so callers can self-document "this text is meant to
// work with a glow" and may later set glow-specific defaults (e.g. tighter
// line-height, different overflow handling, or padding for bloom).
//
// Example:
//   glow::apply_ae_glow(l);
//   l.text("t", glow_text({.text = "GLOW", .font_size = 80, .tracking = 4}));
//
inline TextParams glow_text(CenterTextOptions o,
                            Color /*glow_color*/ = {1.0f, 1.0f, 1.0f, 1.0f},
                            f32 /*radius*/ = 24.0f,
                            f32 /*intensity*/ = 0.6f) {
    // glow_color / radius / intensity are reserved for future use when
    // TextParams carries glow metadata.  For now, glow is applied via
    // the layer builder API.
    return TextParams{
        .text            = std::move(o.text),
        .size            = o.box,
        .pos             = o.pos,
        .font_path       = std::move(o.font_path),
        .font_family     = std::move(o.font_family),
        .font_weight     = o.font_weight,
        .font_style      = std::move(o.font_style),
        .font_size       = o.font_size,
        .color           = o.color,
        .anchor          = TextAnchor::Center,
        .centering_mode  = TextCenteringMode::PixelInk,
        .align           = TextAlign::Center,
        .vertical_align  = VerticalAlign::Middle,
        .line_height     = o.line_height,
        .tracking        = o.tracking,
        .auto_fit        = o.auto_fit,
        .max_lines       = o.max_lines,
        .min_font_size   = o.min_font_size,
        .max_font_size   = o.max_font_size,
        .overflow        = TextOverflow::Clip,
        .wrap            = TextWrap::Word,
    };
}

} // namespace chronon3d::content::text
