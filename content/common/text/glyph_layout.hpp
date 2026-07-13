#pragma once

// ── glyph_layout — FontEngine shaping + per-glyph measurement ────────────
//
// P1 refactor — extracted from `content/common/text_reveal_helpers.hpp`
// (Step 2 of 4).  Single-responsibility: shaping (HarfBuzz) and
// per-glyph layout (positioning).
//
// DEAD CODE REMOVED: the pre-split `shared_glyph_engine(FontEngine&)`
// pass-through (a literal no-op returning its input) had zero callers
// (per code-search of the codebase); removed per AGENTS.md Cat-3
// anti-duplication.  The new module exposes only the genuinely-used
// layout primitives.
//
// Namespace: chronon3d::content::text_reveal (single flat namespace per
// Cat-3 minimal-surface — preserves the 12 existing callers' `using`
// declarations).

#include <chronon3d/core/types/types.hpp>  // f32 (canonical SDK types header)
#include <chronon3d/text/font_engine.hpp>  // FontEngine, FontSpec, GlyphRun

#include <string>
#include <vector>

namespace chronon3d::content::text_reveal {

// Per-glyph position result (centre-X + advance width, post-shaping).
struct GlyphPos {
    std::string ch;
    f32         center_x{0.0f};
    f32         width{0.0f};
};

// measure_text_width — total advance width INCLUDING tracking, matching
// layout_glyphs output.  Returns 0.0f if shaping fails (fail-soft at the
// pre-split's choice; layout_glyphs fail-loud via throw).
f32 measure_text_width(const std::string& text, f32 font_size,
                       const FontSpec& spec, f32 tracking,
                       FontEngine& engine);

// layout_glyphs — per-glyph positions at FINAL locations (only opacity /
// position animate per frame so the text block stays perfectly stable).
// Throws std::runtime_error on HarfBuzz shaping failure (zero glyphs).
std::vector<GlyphPos> layout_glyphs(
    const std::string& text, f32 font_size,
    const FontSpec& spec, f32 tracking,
    f32 ref_offset_x,
    FontEngine& engine);

} // namespace chronon3d::content::text_reveal
