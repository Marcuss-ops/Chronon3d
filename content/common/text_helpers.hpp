#pragma once

// ── Shared Text Helpers ─────────────────────────────────────────────────────
//
// Unified text-building helpers used by ALL composition families (Minimalist,
// Animation, Text, Effects, Grid, etc.) so standard text patterns are
// consistent across the codebase.
//
// Three groups of helpers:
//   1. centered_text  —  one-liner centered text, multi-line blocks
//   2. typewriter     —  character reveal & per-frame helpers
//   3. glow           —  glow-attached text layers & presets
//
// Each group is self-contained in its own namespace so callers can opt in:
//
//   #include "content/common/text_helpers.hpp"
//
//   using namespace chronon3d::content::text_helpers::centered_text;
//
//   SceneBuilder s(ctx);
//   s.layer("hero", [](LayerBuilder& l) {
//       l.pin_to(Anchor::Center);
//       l.text("t", make_centered_text("HELLO WORLD", 72.0f));
//   });
//
//   // Or with glow:
//   s.layer("glow", [](LayerBuilder& l) {
//       l.pin_to(Anchor::Center);
//       apply_ae_glow(l);
//       l.text("t", make_centered_text("GLOW TEXT", 64.0f));
//   });
//

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include <string>
#include <algorithm>

namespace chronon3d::content::text_helpers {

// ═════════════════════════════════════════════════════════════════════════════
// Common typography & color constants
// ═════════════════════════════════════════════════════════════════════════════
inline constexpr const char* FONT_BOLD     = "assets/fonts/Poppins-Bold.ttf";
inline constexpr const char* FONT_REGULAR  = "assets/fonts/Poppins-Regular.ttf";

inline constexpr Color TEXT_WHITE   = {0.95f, 0.96f, 0.98f, 1.0f};
inline constexpr Color TEXT_MUTED   = {0.55f, 0.60f, 0.72f, 1.0f};
inline constexpr Color TEXT_CAPTION = {0.60f, 0.65f, 0.80f, 1.0f};
inline constexpr Color TEXT_DIM     = {0.40f, 0.45f, 0.60f, 1.0f};

// ═════════════════════════════════════════════════════════════════════════════
// 1. Centered Text Helpers
// ═════════════════════════════════════════════════════════════════════════════
namespace centered_text {

// ── make_centered_text ─────────────────────────────────────────────────────
// Standard single-line centered text.  The workhorse helper used by nearly
// every composition — replaces ad-hoc TextParams{...} with .anchor = {Center,
// Middle} that was duplicated across 20+ files.
//
//   make_centered_text("HELLO", 72.0f)
//   make_centered_text("HELLO", 72.0f, {.tracking = 6.0f})
//
struct CenteredTextOpts {
    Vec2   size{900.0f, 160.0f};
    f32    line_height{1.10f};
    f32    tracking{0.0f};
    Vec3   pos{0.0f, 0.0f, 0.0f};
    Color  color{TEXT_WHITE};
    bool   auto_fit{false};
    int    max_lines{1};
    f32    min_font_size{12.0f};
    f32    max_font_size{160.0f};
    TextOverflow overflow{TextOverflow::Clip};
    TextWrap     wrap{TextWrap::None};
};

inline TextParams make_centered_text(std::string text, f32 font_size,
                                     const CenteredTextOpts& opts = {}) {
    return TextParams{
        .text        = std::move(text),
        .size        = opts.size,
        .pos         = opts.pos,
        .font_path   = FONT_BOLD,
        .font_size   = font_size,
        .color       = opts.color,        .anchor     = TextAnchor::Center,
        .align      = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .line_height = opts.line_height,
        .tracking    = opts.tracking,
        .auto_fit    = opts.auto_fit,
        .max_lines   = opts.max_lines,
        .min_font_size = opts.min_font_size,
        .max_font_size = opts.max_font_size,
        .overflow    = opts.overflow,
        .wrap        = opts.wrap,
    };
}

// ── make_left_text ─────────────────────────────────────────────────────────
// Left-aligned text (e.g. lower-thirds, captions, badges).
inline TextParams make_left_text(std::string text, f32 font_size,
                                 const CenteredTextOpts& opts = {}) {
    return TextParams{
        .text        = std::move(text),
        .size        = opts.size,
        .pos         = opts.pos,
        .font_path   = FONT_BOLD,
        .font_size   = font_size,
        .color       = opts.color,        .anchor     = TextAnchor::Center,
        .align      = TextAlign::Left,
        .vertical_align = VerticalAlign::Middle,
        .line_height = opts.line_height,
        .tracking    = opts.tracking,
        .auto_fit    = opts.auto_fit,
        .max_lines   = opts.max_lines,
        .min_font_size = opts.min_font_size,
        .max_font_size = opts.max_font_size,
        .overflow    = opts.overflow,
        .wrap        = opts.wrap,
    };
}

// ── make_text_block ────────────────────────────────────────────────────────
// Multi-line centered text block with wider box and word wrap.  Ideal for
// paragraphs, subtitles, or any text that needs more than one line.
inline TextParams make_text_block(std::string text, f32 font_size,
                                  const CenteredTextOpts& opts = {}) {
    CenteredTextOpts block_opts = opts;
    if (opts.size == Vec2{900.0f, 160.0f})
        block_opts.size = {1300.0f, 360.0f};
    if (opts.max_lines == 1)
        block_opts.max_lines = 4;
    if (!opts.auto_fit)
        block_opts.auto_fit = true;
    if (opts.wrap == TextWrap::None)
        block_opts.wrap = TextWrap::Word;
    return make_centered_text(std::move(text), font_size, block_opts);
}

} // namespace centered_text

// ═════════════════════════════════════════════════════════════════════════════
// 2. Typewriter Helpers
// ═════════════════════════════════════════════════════════════════════════════
namespace typewriter {

// ── reveal_text_by_frame ───────────────────────────────────────────────────
// Reveals text one character at a time.  Returns a single space when nothing
// is visible yet so the text layer does not collapse to zero size.
//
// Usage (in a composition callback):
//   std::string visible = reveal_text_by_frame("HELLO", ctx.frame(), 2.0f);
//   s.layer("text", [&](auto& l) {
//       l.text("t", centered_text::make_centered_text(visible, 64.0f));
//   });
//
inline std::string reveal_text_by_frame(const std::string& full_text,
                                        Frame frame,
                                        f32 chars_per_frame = 2.0f) {
    size_t visible = std::min(
        full_text.size(),
        static_cast<size_t>(static_cast<f32>(frame) * chars_per_frame));
    if (visible == 0) return " ";
    return full_text.substr(0, visible);
}

// ── typewriter_progress ────────────────────────────────────────────────────
// Returns a normalised [0..1] progress value for the typewriter effect,
// useful for driving secondary animations (glow settle, blur fade, etc.).
inline f32 typewriter_progress(const std::string& full_text,
                               Frame frame,
                               f32 chars_per_frame = 2.0f) {
    if (full_text.empty()) return 1.0f;
    f32 total_chars = static_cast<f32>(full_text.size());
    f32 revealed = std::min(static_cast<f32>(frame) * chars_per_frame,
                            total_chars);
    return revealed / total_chars;
}

} // namespace typewriter

// ═════════════════════════════════════════════════════════════════════════════
// 3. Glow Helpers
// ═════════════════════════════════════════════════════════════════════════════
namespace glow {

// ── GlowTextOpts ───────────────────────────────────────────────────────────
struct GlowTextOpts {
    f32   font_size{64.0f};
    Vec2  text_size{700.0f, 160.0f};
    Color text_color{TEXT_WHITE};
    f32   tracking{4.0f};
    f32   line_height{1.10f};

    // V2 TextGlowSpec radii & intensities (ae_cinematic_white defaults)
    f32 inner_radius    {4.0f};
    f32 mid_radius      {14.0f};
    f32 bloom_radius    {34.0f};
    f32 inner_intensity {0.55f};
    f32 mid_intensity   {0.22f};
    f32 bloom_intensity {0.08f};

    // Micro shadow (the "contact" shadow that anchors the bloom)
    bool  micro_shadow    {true};
    Vec2  shadow_offset   {0.0f, 4.0f};
    Color shadow_color    {0.0f, 0.02f, 0.12f, 0.15f};
    f32   shadow_blur     {10.0f};

    // Overall drop shadow (optional)
    bool  drop_shadow     {false};
    Vec2  drop_offset     {0.0f, 8.0f};
    Color drop_color      {0.0f, 0.0f, 0.0f, 0.20f};
    f32   drop_blur       {18.0f};
};

// ── apply_ae_glow ──────────────────────────────────────────────────────────
// Applies the AE-cinematic glow preset to a LayerBuilder.  This is the
// recommended V2 glow API — use on any text layer that needs a polished,
// multi-radius glow with micro shadow.
//
//   s.layer("hero", [](LayerBuilder& l) {
//       l.pin_to(Anchor::Center);
//       apply_ae_glow(l);
//       l.text("t", centered_text::make_centered_text("GLOW", 64.0f));
//   });
//
inline void apply_ae_glow(LayerBuilder& l, const GlowTextOpts& opts = {}) {
    auto glow = TextGlowPresets::ae_cinematic_white();
    glow.inner_radius    = opts.inner_radius;
    glow.mid_radius      = opts.mid_radius;
    glow.bloom_radius    = opts.bloom_radius;
    glow.inner_intensity = opts.inner_intensity;
    glow.mid_intensity   = opts.mid_intensity;
    glow.bloom_intensity = opts.bloom_intensity;

    l.glow(glow.to_glow_params());

    if (opts.micro_shadow && glow.micro_shadow) {
        l.drop_shadow(opts.shadow_offset, opts.shadow_color, opts.shadow_blur);
    }
    if (opts.drop_shadow) {
        l.drop_shadow(opts.drop_offset, opts.drop_color, opts.drop_blur);
    }
}

// ── add_glow_text_layer ────────────────────────────────────────────────────
// Convenience: adds a centered, glow-attached text layer in one call.
inline void add_glow_text_layer(SceneBuilder& s,
                                const std::string& layer_name,
                                const std::string& text,
                                f32 font_size,
                                const GlowTextOpts& opts = {}) {
    s.layer(layer_name, [&](LayerBuilder& l) {
        l.pin_to(Anchor::Center);
        apply_ae_glow(l, opts);
        l.text("t", {
            .text        = text,
            .size        = opts.text_size,
            .pos         = {0.0f, 0.0f, 0.0f},
            .font_path   = FONT_BOLD,
            .font_size   = font_size,
            .color       = opts.text_color,
            .anchor      = {TextAlign::Center, VerticalAlign::Middle},
            .line_height = opts.line_height,
            .tracking    = opts.tracking,
        });
    });
}

// ── add_bloom_reveal_layer ─────────────────────────────────────────────────
// Adds a text layer with bloom-settle glow: the glow overshoots at its
// maximum radius then settles to the configured radii.  Useful for dramatic
// reveals where the text appears with a flash and then the bloom tightens.
//
// Note: this does NOT do character-by-character reveal — use
// typewriter::reveal_text_by_frame() in the composition callback for that.
//
//   SceneBuilder s(ctx);
//   add_bloom_reveal_layer(s, "hero", "TITLE", 64.0f,
//       {.bloom_start_radius = 48.0f, .bloom_settle_radius = 18.0f});
//
struct BloomRevealOpts {
    f32   font_size{64.0f};
    Vec2  text_size{700.0f, 160.0f};
    Color text_color{TEXT_WHITE};
    f32   tracking{4.0f};
    f32   line_height{1.10f};

    // Bloom-settle parameters
    f32 bloom_intensity   {0.30f};
    f32 bloom_start_radius{40.0f};
    f32 bloom_settle_radius{14.0f};

    // Base radii (what the glow settles to)
    f32 inner_radius    {4.0f};
    f32 mid_radius      {14.0f};
    f32 bloom_radius    {34.0f};
    f32 inner_intensity {0.55f};
    f32 mid_intensity   {0.22f};

    // Micro shadow
    bool  micro_shadow  {true};
    Vec2  shadow_offset {0.0f, 4.0f};
    Color shadow_color  {0.0f, 0.02f, 0.12f, 0.15f};
    f32   shadow_blur   {10.0f};
};

inline void add_bloom_reveal_layer(SceneBuilder& s,
                                   const std::string& layer_name,
                                   const std::string& text,
                                   f32 font_size,
                                   const BloomRevealOpts& opts = {}) {
    s.layer(layer_name, [&](LayerBuilder& l) {
        l.pin_to(Anchor::Center);

        auto glow = TextGlowPresets::ae_cinematic_white();
        glow.inner_radius    = opts.inner_radius + opts.bloom_start_radius * 0.25f;
        glow.mid_radius      = opts.bloom_settle_radius + opts.bloom_start_radius * 0.4f;
        glow.bloom_radius    = opts.bloom_start_radius;
        glow.inner_intensity = opts.inner_intensity + opts.bloom_intensity * 0.5f;
        glow.mid_intensity   = opts.mid_intensity   + opts.bloom_intensity * 0.4f;
        glow.bloom_intensity = opts.bloom_intensity;
        l.glow(glow.to_glow_params());

        if (opts.micro_shadow && glow.micro_shadow) {
            l.drop_shadow(opts.shadow_offset, opts.shadow_color, opts.shadow_blur);
        }

        l.text("t", {
            .text        = text,
            .size        = opts.text_size,
            .pos         = {0.0f, 0.0f, 0.0f},
            .font_path   = FONT_BOLD,
            .font_size   = font_size,
            .color       = opts.text_color,
            .anchor      = {TextAlign::Center, VerticalAlign::Middle},
            .line_height = opts.line_height,
            .tracking    = opts.tracking,
        });
    });
}

} // namespace glow

} // namespace chronon3d::content::text_helpers
