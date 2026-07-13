#pragma once

// ── typewriter_block — 2-line per-glyph typewriter reveal ────────────────
//
// P1 refactor — extracted from `content/common/text_reveal_helpers.hpp`
// (Step 4 of 4).  Single-responsibility: the shared 2-line typewriter block
// (used by both the production compositions and the A/B test in
// tests/visual/glow_ab/).
//
// refactor(typewriter): TwoLineTypewriterSpec drives Anim* (Point 11) —
// replaces the 12-default-arg positional signature with a struct-driven
// API + a return struct (TypewriterBlockResult) carrying the second-line
// right-edge x-coord + last-char reveal frame.  Caller-factory reductions
// bring AnimTypewriterSimple/Slide/Glow from ~30-50 LoC each to ~10-15 LoC.
//
// DEPENDENCIES: text_reveal (for build_text_reveal_line + TextRevealDescriptor
// + font_regular) + glyph_layout (for shape_glyph_line — Post-Point-8
// single-shape contract: 1 measure_text_width-style shape call returns
// {width, glyphs-vector} for both w + n_glyphs in one shape call).
//
// Namespace: chronon3d::content::text_reveal (single flat namespace per
// Cat-3 minimal-surface — preserves the 6+ existing callers' `using`
// declarations).

#include <chronon3d/core/types/types.hpp>          // f32, Color, Frame (canonical SDK types header)
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <string>

namespace chronon3d::content::text_reveal {

// TextLineSpec — minimal per-line text content + font size.
// Internal to TwoLineTypewriterSpec (Cat-3 minimal-surface; not promoted
// to include/chronon3d/).
struct TextLineSpec {
    std::string text;
    f32         font_size{72.0f};
};

// TwoLineTypewriterSpec — drives build_2line_typewriter.  All values are
// inline-defaulted to the production defaults used by AnimTypewriterSimple
// (preserves pre-Point-11 byte-output verbatim when callers omit fields).
//
// Field renames vs the pre-Point-11 positional signature:
//   start_delay_2 → second_delay
// (others: same name; existing defaults preserved).
struct TwoLineTypewriterSpec {
    TextLineSpec first;
    TextLineSpec second;

    f32  second_delay{36.0f};          // frames before line 2 starts revealing
    f32  line_spacing{85.0f};           // pixel distance between line 1 / line 2 y-coords
    f32  base_y{-50.0f};                // y-coord of the line-1/line-2 center axis
    f32  tracking{4.0f};                // per-glyph tracking spacing
    bool slide_up{false};               // chars slide up during reveal (each pulls from y+30)
    f32  glow_intensity{0.0f};          // 0=no glow; >0 micro-glow on revealed chars

    Color text_color{0.92f, 0.93f, 0.97f, 1.0f};   // slightly off-white production text color
    Color shadow_color{0.0f, 0.0f, 0.0f, 0.15f};   // semi-transparent black drop shadow
};

// TypewriterBlockResult — output of build_2line_typewriter, carrying
// pre-computed geometry for downstream consumers (e.g. add_cursor on
// AnimTypewriterCursor).
//
// `second_line_right_edge` = ref_x + w2 (left-aligned x of line 2 left
// edge + line 2's full advance width INCLUDING tracking — no gap).
//
// `second_line_reveal_end` = the Frame at which the LAST glyph of line 2
// is fully revealed (i.e. start_delay + (n_glyphs - 1) * stagger + duration).
// Canonical formula — consumers that want a post-reveal pause add their
// own buffer (e.g. AnimTypewriterCursor adds a 6-frame gap to match the
// pre-refactor visual pause of cursor_delay = reveal_end + 6).
struct TypewriterBlockResult {
    f32   second_line_right_edge{0.0f};
    Frame second_line_reveal_end{Frame{0}};
};

// build_2line_typewriter — centered 2-line per-glyph typewriter reveal.
// Returns the second-line geometry for downstream consumers (e.g. cursor
// placement on AnimTypewriterCursor) without requiring the caller to
// recompute widths or character counts.
//
// Replaces the 12-positional-args signature from pre-Point-11.  Cat-3
// minimal-surface: NO overload — the old signature is GONE, callers must
// migrate to TwoLineTypewriterSpec.
TypewriterBlockResult build_2line_typewriter(
    SceneBuilder& s,
    const TwoLineTypewriterSpec& spec);

} // namespace chronon3d::content::text_reveal
