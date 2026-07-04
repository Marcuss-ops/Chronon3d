// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/backends/text/blend2d_glyph_conversion.hpp
//
// M1.5#11 — reusable Blend2D text-glyph conversion utility module,
// extracted from the legacy text_rasterizer_render.cpp and named with
// explicit semantic intent (per user-spec "moduli con nomi espliciti").
//
// Internal-only header: lives in src/backends/text/ alongside sibling
// extraction files (`text_rasterizer_cache.cpp`, `text_rasterizer_ink.cpp`).
// NOT in `include/chronon3d/` — these utilities are internal-to-backend
// and do NOT expand the public ABI surface (AGENTS.md Cat-5 compliant).
//
// What this header exposes:
//   * `apply_text_style`        — unified BLContext fill/stroke style
//                                  applicator with `StyleOp` discriminator
//                                  (deduplicates previously-mirrored
//                                  apply_text_fill_style / apply_text_stroke_style
//                                  textual mirror images).
//   * `apply_text_fill_style`   — thin wrapper preserving the legacy
//                                  TextStyle&-and-fallback-colour fill
//                                  call-site signature.
//   * `apply_text_stroke_style` — thin wrapper for stroke.
//   * `HbToBlGlyphRun` struct    — owned-buffer RAII handle for the
//                                  HarfBuzz placed-glyph → Blend2D
//                                  BLGlyphRun data conversion, with
//                                  static `from(...)` factory performing
//                                  design-unit scaling (Blend2D
//                                  ADVANCE_OFFSET expects font-design
//                                  units; HarfBuzz provides pixels 26.6).
//
// Anti-duplication invariant (user constraint M1.5#11):
//   This module contains ZERO caching logic.  Font-face loading
//   (BLFontFace createFromFile) lives in M1.5#7's BLFontFaceCache
//   (per-session `TextRenderResources.bl_faces`).  `Blend2DResources`
//   (the legacy cache inside text_rasterizer_render.cpp) is kept
//   ONLY for the legacy function's lifetime — it gets deleted with
//   M1.5#9 (Step 5 — rasterizer infrastructure deletion).
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <blend2d.h>

#include <optional>
#include <vector>

#include <chronon3d/scene/model/shape/shape.hpp>     // TextStyle, Fill, Color
#include <chronon3d/text/font_engine.hpp>             // PlacedGlyphRun

#include "../software/utils/blend2d_paint.hpp"        // StyleOp + to_bl_rgba + build_bl_gradient

namespace chronon3d {

// ── apply_text_style — unified BLContext fill/stroke style applicator ─────
//
// Apply a `Fill` to `ctx` as either fill or stroke, delegating to the
// shared `blend2d_bridge::paint::build_bl_gradient` for gradient
// construction.  `op` discriminates `setFillStyle` vs `setStrokeStyle`
// so the legacy duplication between `apply_text_fill_style` and
// `apply_text_stroke_style` (~90 lines each, textual mirror images)
// collapses to one helper.
//
// `style_opt` may be empty, in which case `fallback_color` is used as
// the solid fill.  Interior `Fill` instances of `Solid` type use
// `to_bl_rgba` directly; gradient types delegate to `build_bl_gradient`.
// On gradient failure, the fallback colour kicks in (defensive default).
void apply_text_style(
    BLContext&                                   ctx,
    chronon3d::blend2d_bridge::paint::StyleOp    op,
    const std::optional<Fill>&                   style_opt,
    const Color&                                 fallback_color,
    float                                        origin_x,
    float                                        origin_y,
    float                                        width,
    float                                        height
);

// ── apply_text_fill_style — legacy TextStyle& wrapper for fill ────────────
void apply_text_fill_style(
    BLContext&           ctx,
    const TextStyle&     style,
    const Color&         fallback_color,
    float                origin_x,
    float                origin_y,
    float                width,
    float                height
);

// ── apply_text_stroke_style — legacy TextStyle& wrapper for stroke ────────
void apply_text_stroke_style(
    BLContext&           ctx,
    const TextStyle&     style,
    const Color&         fallback_stroke_color,
    float                origin_x,
    float                origin_y,
    float                width,
    float                height
);

// ── HbToBlGlyphRun — HarfBuzz placed-glyph → Blend2D BLGlyphRun converter ─
//
// The struct owns the underlying glyph-id + placement vectors and
// configures a `BLGlyphRun` POD that points into those vectors.
// IMPORTANT: the struct must outlive the BLGlyphRun (which holds raw
// pointers into the vectors).  Keep the struct alive through the
// rendering call that consumes `bl_run`.
//
// Uses BL_GLYPH_PLACEMENT_TYPE_ADVANCE_OFFSET: placement is a relative
// offset from the current pen position, and after each glyph the pen is
// advanced by `advance`.  We therefore use the raw HarfBuzz relative
// offsets (x_offset / y_offset), NOT the cumulative x / y positions.
//
// IMPORTANT: fillGlyphRun with ADVANCE_OFFSET expects placement values
// in FONT DESIGN UNITS, not pixels.  HarfBuzz provides values in pixels
// (26.6 fixed-point ÷ 64).  This conversion multiplies by
// (upem / font_size) to convert pixels to design units, matching
// Blend2D's expectations.
//
// The tracking-aware advances come pre-computed from PlacedGlyphRun,
// so this conversion handles field copies + design-unit scaling; no
// additional HarfBuzz calls are made by the factory.
struct HbToBlGlyphRun {
    std::vector<uint32_t>           glyph_ids;
    std::vector<BLGlyphPlacement>   placements;
    BLGlyphRun                      bl_run{};

    /// Convert a PlacedGlyphRun into a Blend2D BLGlyphRun.
    /// Uses the raw offsets (x_offset, y_offset) and tracking-aware
    /// advances from the PlacedGlyphRun, matching the ADVANCE_OFFSET
    /// placement type.  Scales pixel values to font design units.
    static HbToBlGlyphRun from(
        const PlacedGlyphRun&   placed,
        const BLFontFace&       face,
        float                   font_size
    );
};

} // namespace chronon3d
