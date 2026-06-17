#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// ImportantWords Theme — palette-presets + word sizing.
//
// A composition in this category is one uppercase word glued to a more
// RECTANGULAR rounded-rect backdrop (height 140 px, corner radius 4 px —
// deliberately less squircle than the previous draft so it reads as a
// proper "label" / "tag" shape rather than a soft chip).  Each composition
// picks one of three PALETTES so the rectangle's RED shade is visibly
// distinct from the other roles in the Trio (coral / vermillion / magenta).
//
// Visual recipe (per user feedback, latest pass):
//   • Backdrop = VERY SATURATED RED shade, fully opaque so the rectangle
//     reads against the fully-black canvas.
//   • Text     = WHITE, Inter-Bold (more "modern" geometric sans than the
//     previous Poppins).
//   • Shadow   = BLACK, 16 px blur, 6 px down — wider / SOFTER falloff so
//     the shadow reads as a diffuse contact shadow rather than a hard
//     drop shadow.  Alpha dropped to 0.55 so the shadow recedes instead
//     of dominating.
//   • No accent line — by design.
//
// Adding a new palette: declare an extra inline constexpr below and pick
// it in the composition's build_important_word() call.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d::content::important_words {

// ── WordPalette — red-on-white palette pair ─────────────────────────────────
// Each entry is opaque so the rectangle READS against the black canvas.
// `text` is fixed to (1,1,1,1) across all palettes (always white).
// `shadow` is fixed to a SOFT black @ 0.55 alpha — shared geometry, only
// the alpha differs slightly between palettes for darker backdrops.
struct WordPalette {
    Color backdrop;   // RED rectangle fill, alpha 1.0 so it is always visible
    Color text;       // word colour  (always pure white)
    Color shadow;     // drop-shadow colour (always soft black @ ~0.55)
};

// ── Three predefined palettes — HUE rotation boosted saturation ────────────
// "più acceso" — vivid reds.  Coral reads coral-warm, vermillion reads pure
// red, magenta-red reads cool-pink-red.  Per-pair ΔR/ΔG/ΔB emphasise the
// rotation so the Trio is visibly distinct:

// PALETTE_LIGHT — coral red (warm coral, R→G rotates towards orange)
inline constexpr WordPalette PALETTE_LIGHT = {
    .backdrop = { 0.98f, 0.45f, 0.30f, 1.0f },
    .text     = { 1.00f, 1.00f, 1.00f, 1.0f },
    .shadow   = { 0.00f, 0.00f, 0.00f, 0.55f },
};
// PALETTE_WARM — vermillion red (pure red, balanced)
inline constexpr WordPalette PALETTE_WARM = {
    .backdrop = { 0.95f, 0.18f, 0.12f, 1.0f },
    .text     = { 1.00f, 1.00f, 1.00f, 1.0f },
    .shadow   = { 0.00f, 0.00f, 0.00f, 0.55f },
};
// PALETTE_COOL — magenta-red (R→B rotates toward magenta/red-violet)
inline constexpr WordPalette PALETTE_COOL = {
    .backdrop = { 0.85f, 0.10f, 0.34f, 1.0f },
    .text     = { 1.00f, 1.00f, 1.00f, 1.0f },
    .shadow   = { 0.00f, 0.00f, 0.00f, 0.60f },
};

// ── WordPreset: a single uppercase word + its sized backdrop ───────────────
// `rect_outer_size` is pre-measured for the word's advance width at
// font_size 80 Inter-Bold with tracking 10.  No palette — the user
// composes a WordPreset with an explicit WordPalette when calling the
// build helper.
struct WordPreset {
    const char* label;
    Vec2 rect_outer_size;   // backdrop rectangle (px)
    f32  font_size;
    f32  tracking;
    f32  pad_x;
    f32  pad_y;
    f32  corner_radius;     // rounded_rect corner radius (lower = more rect)
};

// ── Three canonical presets (lower-third important words) ──────────────────
// Heigh bumped 110 → 140 so the backdrop reads as a more elongated /
// rectangular label (vs. previous squircle chip).  Corner radius dropped
// from 10 → 4 to remove the "soft chip" feel and keep a cleaner rect look.
// Widths preserved at the previously-tuned 480/320/380 px.
inline constexpr WordPreset WORD_DIRECTOR = {
    .label = "DIRECTOR",
    .rect_outer_size = { 480.0f, 140.0f },
    .font_size = 80.0f,
    .tracking  = 10.0f,
    .pad_x     = 36.0f,
    .pad_y     = 32.0f,
    .corner_radius = 4.0f,
};
inline constexpr WordPreset WORD_ACTOR = {
    .label = "ACTOR",
    .rect_outer_size = { 320.0f, 140.0f },
    .font_size = 80.0f,
    .tracking  = 10.0f,
    .pad_x     = 36.0f,
    .pad_y     = 32.0f,
    .corner_radius = 4.0f,
};
inline constexpr WordPreset WORD_WRITER = {
    .label = "WRITER",
    .rect_outer_size = { 380.0f, 140.0f },
    .font_size = 80.0f,
    .tracking  = 10.0f,
    .pad_x     = 36.0f,
    .pad_y     = 32.0f,
    .corner_radius = 4.0f,
};

// Lower-third y offset (canvas is 1920×1080).  +360 sits at the 67% line.
inline constexpr f32 WORD_LOWER_Y = 360.0f;

// ── Drop shadow tuning — SOFT + diffuse ─────────────────────────────────────
// Shadow geometry changed for a more diffuse / less-harsh look:
//   • offset (0, 4) → (0, 6) — slightly more drift down
//   • radius 8 → 16 — wider blur kernel
//   • alpha 0.85-0.92 → ~0.55-0.60 (per palette.shadow) — soft fall-off
//
// These are exposed as named constants so a renderer can reuse the same
// look across all 3 palettes.
inline constexpr Vec2  WORD_SHADOW_OFFSET = { 0.0f, 6.0f };
inline constexpr f32   WORD_SHADOW_RADIUS = 16.0f;

// ── Font face (modern: Inter-Bold replaces Poppins-Bold) ───────────────────
// Inter is the codebase-wide default (`assets/fonts/Inter-Bold.ttf`); we
// expose the path + family here so all callsites use the same face.
inline constexpr const char* WORD_FONT_PATH   = "assets/fonts/Inter-Bold.ttf";
inline constexpr const char* WORD_FONT_FAMILY = "Inter";

} // namespace chronon3d::content::important_words
