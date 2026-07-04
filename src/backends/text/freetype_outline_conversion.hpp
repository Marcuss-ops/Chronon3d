// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/backends/text/freetype_outline_conversion.hpp
//
// M1.5#11 — reusable FreeType outline → Blend2D BLPath converter, extracted
// from the legacy `text_rasterizer_render.cpp::FtGlyphPathBuilder::build_path`
// body.  Named with explicit semantic intent (per user-spec "moduli con nomi
// espliciti": `freetype_outline_conversion`).
//
// Internal-only header: lives in src/backends/text/, NOT in
// `include/chronon3d/`.  Pure outline-decoder utility — does NOT open
// or cache `FT_Face` (face I/O belongs to M1.5#7's `FreeTypeFaceCache`
// per-session API) and does NOT move the face-load coupling from the
// legacy `FtGlyphPathBuilder::load_face`, which stays LOCAL to
// `text_rasterizer_render.cpp` until M1.5#9 Step 5 deletes the legacy
// rasterizer Tup.
//
// Anti-duplication invariant (user constraint M1.5#11 + M1.5#6+#7):
//   * This module is a pure outline-decode utility — ZERO caching logic.
//   * Font-face I/O lives in M1.5#7's `FreeTypeFaceCache` (per-session).
//   * Glyph-to-outline path conversion lives in M1.5#7's
//     `GlyphOutlineBuilder::build_path(FT_Face, u32, ...)` (per-call,
//     per-glyph, NOT batched).
//   * The BATCHED multi-glyph iteration (used by the legacy stroke path
//     that needs ALL glyph outlines in one BLPath for contextually-fused
//     rendering of Arabic / Devanagari / etc.) lived in the legacy
//     `FtGlyphPathBuilder::build_path(const PlacedGlyphRun&, ...)` body.
//     It is preserved here as `convert_ft_outline_to_bl_path(FT_Face,
//     const PlacedGlyphRun&, ...)` — same signature shape, ZERO cache,
//     caller-owned face lifecycle.
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <blend2d.h>

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include <chronon3d/text/font_engine.hpp>     // PlacedGlyphRun

namespace chronon3d {

#ifdef CHRONON3D_ENABLE_TEXT

/// Convert a HarfBuzz-shaped `PlacedGlyphRun` into a Blend2D `BLPath`,
/// decomposing each glyph's `FT_Outline` via `FT_Outline_Decompose`.
///
/// Thread-safety: caller is responsible for holding the FT_Face's lock
/// during the call — concurrent `FT_Load_Glyph` / `FT_Set_Pixel_Sizes`
/// on the same FT_Face is undefined behaviour (per FreeType docs).
/// The legacy `FtGlyphPathBuilder::load_face` already exposes a
/// `std::mutex` for this purpose; callers that don't have access to a
/// freezable FT_Face handle should batch the conversions against their
/// own synchronization primitive.
///
/// Y-axis mirror: font-space Y is up; Blend2D image-space Y is down.
/// The function negates `FT_Vector.y` (i.e. `ctx.oy - y * kScale`) to
/// bring the outline into the rendering orientation expected by the
/// downstream `ctx.strokePath(...)` consumer.
///
/// Returns an empty `BLPath` if the FT_Face is null or the
/// `PlacedGlyphRun::glyphs` array is empty.
///
/// Glyphs that fail to load (`FT_Load_Glyph != 0`) or that lack an
/// outline (`FT_GLYPH_FORMAT_OUTLINE != format`) are SILENTLY skipped
/// (matching the legacy `FtGlyphPathBuilder::build_path` semantics).
[[nodiscard]] BLPath convert_ft_outline_to_bl_path(
    FT_Face                   ft_face,
    const PlacedGlyphRun&     placed,
    float                     origin_x,
    float                     origin_y
);

#endif // CHRONON3D_ENABLE_TEXT

} // namespace chronon3d
