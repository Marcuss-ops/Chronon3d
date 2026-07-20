// ---------------------------------------------------------------------------
// text_run_geometry.cpp — Backend-independent text run bbox computation
//
// Extracted from backends/software/processors/text_run/text_run_processor.cpp
// so the render graph can compute bounding boxes without linking the software
// backend.
//
// Canonical per-glyph bbox accumulator: compute_text_run_visual_bounds()
// Used by:
//   - compute_text_run_world_bbox (adds model transform + spread)
//   - software rasterizer prepare stage (uses local-space directly)
//   - future cache/tile pruning, debug overlay, etc.
//
// TICKET-TEXT-CLIP-ASCENT — baseline-anchored bbox math.
// Previously the local-bbox accumulator used `min_y = gy - pad` and
// `max_y = gy + pad + placed.total_height`, treating `gy` (the
// baseline of the glyph) as if it were the top of the glyph ink.  That
// cut off ~80% of ascender extents and produced scratch surfaces large
// enough for only the descender strip — visually identical to seeing
// a 19 px sliver of text in a 1080-row canvas.  See
// tests/text_golden/text_clip/text_clip_bounds.cpp for the regression
// lock that captures this symptom numerically.
// ---------------------------------------------------------------------------

#include <chronon3d/text/text_run_geometry.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace chronon3d::renderer {

// ═══════════════════════════════════════════════════════════════════════════
// compute_text_run_visual_bounds — canonical local-space bbox accumulator
// ═══════════════════════════════════════════════════════════════════════════
//
// Single source of truth for per-glyph visual bounds.  Walks both active
// and crossfade glyph vectors, accounting for:
//   - layout position + animated offset
//   - blur + stroke width + safety padding (8px)
//   - 2.5D shear estimates (rotation.x/y tangent projection)
//   - scale.z expansion
//   - per-glyph outline bbox from `placed.glyphs[i].bbox_*`
//     (FreeType glyph metrics populated during shaping; replaces the
//      previous advance-based approximation so the predicted bbox
//      matches the actual ink extents).
//   - blur + stroke width + safety padding (8px)
//   - 2.5D shear estimates (rotation.x/y tangent projection)
//   - scale.z expansion
//
// Excludes: model transform, world-space projection, shadow padding,
// spread (shadow/glow), and rasterizer margin.  Those are caller concerns.

std::optional<TextRunLocalBounds> compute_text_run_visual_bounds(
    const TextRunShape& shape
) {
    const bool has_active =
        shape.layout != nullptr && !shape.glyphs.empty();
    const bool has_crossfade =
        shape.crossfade_layout != nullptr && !shape.crossfade_glyphs.empty();

    if (!has_active && !has_crossfade) {
        return std::nullopt;
    }

    float min_x = 1e10f, max_x = -1e10f;
    float min_y = 1e10f, max_y = -1e10f;

    auto accumulate =
        [&](const std::vector<GlyphInstanceState>& glyphs,
            const PlacedGlyphRun& placed) {
            // TICKET-TEXT-CLIP-ASCENT — baseline-anchored bbox math.
            // min_y: baseline - ascent (top of glyph ink).
            // max_y: baseline + descent (bottom of glyph ink).
            //   The old code used `gy - pad` for min_y which
            //   anchored the bbox to the baseline (= ~80% of ascender
            //   extents clipped).  Use the actual font ascent/descent
            //   so the scratch raster surface includes both ascender
            //   and descender extents.
            // Defensive floor: when the shaping engine returns zero
            // or tiny ascent/descent (e.g. missing font metrics), fall
            // back to standard typographic ratios (ascent ≈ 80%,
            // descent ≈ 20% of font size).  Without this floor, the
            // scratch surface is sized to pad-only (~8px) and clips
            // all glyph ink.  font_size is the canonical fallback
            // because it is always populated by the text compiler.
            const float font_size = shape.layout ? shape.layout->font_size : 16.0f;
            const float ascent  = std::max({0.0f, placed.ascent, font_size * 0.8f});
            const float descent = std::max({0.0f, placed.descent, font_size * 0.2f});
            for (std::size_t i = 0; i < glyphs.size(); ++i) {
                const auto& g = glyphs[i];
                const auto& pg = placed.glyphs[i];
                const float gx = g.layout_position.x + g.position.x;
                const float gy = g.layout_position.y + g.position.y;
                // 2.5D-aware padding: per-glyph shears can swing the
                // visible extent far beyond layout + position,
                // especially with extreme rotation.x/y or large
                // scale.z.  Estimate worst-case expansion from those.
                const float shear_x_extra = std::abs(std::tan(
                    std::clamp(
                        static_cast<float>(g.rotation.y)
                            * (3.14159265f / 180.0f),
                        -1.5607f, 1.5607f)))
                    * std::abs(static_cast<float>(g.layout_position.y));
                const float shear_y_extra = std::abs(std::tan(
                    std::clamp(
                        static_cast<float>(g.rotation.x)
                            * (3.14159265f / 180.0f),
                        -1.5607f, 1.5607f)))
                    * std::abs(static_cast<float>(g.layout_position.x));
                // `pad` adds only shear-based padding (rotation ×
                // layout_position tangent) plus per-glyph blur/stroke.
                const float pad = g.blur + g.stroke_width + 8.0f
                    + shear_x_extra + shear_y_extra;
                // Per-axis scale combines local (x,y) axes with the
                // 2.5D depth scale (scale.z acts as a uniform expansion
                // multiplier for the planar glyph extents).
                const float scale_x = std::abs(g.scale.x * g.scale.z);
                const float scale_y = std::abs(g.scale.y * g.scale.z);

                // Use the FreeType outline bbox populated during shaping
                // as the geometric source of truth for glyph ink.  This
                // replaces the previous advance-based approximation that
                // under-reported overhanging glyphs (italic, wide chars,
                // emoji, etc.) and caused the predicted bbox to clip real
                // ink or overflow the canvas.
                const bool has_bbox =
                    (pg.bbox_x0 != 0.0f || pg.bbox_x1 != 0.0f ||
                     pg.bbox_y0 != 0.0f || pg.bbox_y1 != 0.0f);
                float glyph_left, glyph_right, glyph_top, glyph_bottom;
                if (has_bbox) {
                    // bbox_* are relative to the glyph origin.  y is
                    // positive-up (FreeType convention), so the screen
                    // top is `gy - bbox_y0` and the screen bottom is
                    // `gy - bbox_y1`.
                    glyph_left   = gx + pg.bbox_x0 * scale_x;
                    glyph_right  = gx + pg.bbox_x1 * scale_x;
                    glyph_top    = gy - pg.bbox_y0 * scale_y;
                    glyph_bottom = gy - pg.bbox_y1 * scale_y;
                } else {
                    // Fallback for zero-bbox glyphs (e.g. spaces):
                    // advance gives the run width and line metrics give
                    // the vertical extent.
                    const float raw_advance = std::abs(pg.advance_x);
                    const float advance = std::max(1.0f, raw_advance);
                    glyph_left   = gx;
                    glyph_right  = gx + advance * scale_x;
                    glyph_top    = gy - ascent * scale_y;
                    glyph_bottom = gy + descent * scale_y;
                }

                min_x = std::min(min_x, glyph_left - pad);
                max_x = std::max(max_x, glyph_right + pad);
                min_y = std::min(min_y, glyph_top - pad);
                max_y = std::max(max_y, glyph_bottom + pad);
            }
        };

    // Walk the active slot first, then the crossfade slot.  The min/
    // max accumulators automatically union the two local-space boxes
    // (rather than eager-clone), so a shorter crossfade text doesn't
    // clip the longer active text and vice versa.
    if (has_active) {
        accumulate(shape.glyphs, shape.layout->placed);
    }
    if (has_crossfade) {
        accumulate(shape.crossfade_glyphs,
                  shape.crossfade_layout->placed);
    }

    return TextRunLocalBounds{min_x, min_y, max_x, max_y};
}

// ═══════════════════════════════════════════════════════════════════════════
// compute_text_run_world_bbox
// ═══════════════════════════════════════════════════════════════════════════
//
// Delegates to compute_text_run_visual_bounds() for local-space bounds,
// then transforms the four corners by @p model and pads by @p spread.

raster::BBox compute_text_run_world_bbox(
    const TextRunShape& shape,
    const Mat4& model,
    f32 spread
) {
    auto local = compute_text_run_visual_bounds(shape);
    if (!local) {
        return {0, 0, 0, 0};
    }

    const float min_x = local->min_x;
    const float min_y = local->min_y;
    const float max_x = local->max_x;
    const float max_y = local->max_y;

    // Transform corners to world space
    Vec4 corners[4] = {
        model * Vec4(min_x, min_y, 0.0f, 1.0f),
        model * Vec4(max_x, min_y, 0.0f, 1.0f),
        model * Vec4(max_x, max_y, 0.0f, 1.0f),
        model * Vec4(min_x, max_y, 0.0f, 1.0f)
    };

    f32 wx_min = 1e10f, wx_max = -1e10f;
    f32 wy_min = 1e10f, wy_max = -1e10f;
    for (const auto& c : corners) {
        if (std::abs(c.w) > 1e-7f) {
            wx_min = std::min(wx_min, c.x / c.w);
            wx_max = std::max(wx_max, c.x / c.w);
            wy_min = std::min(wy_min, c.y / c.w);
            wy_max = std::max(wy_max, c.y / c.w);
        } else {
            wx_min = std::min(wx_min, c.x);
            wx_max = std::max(wx_max, c.x);
            wy_min = std::min(wy_min, c.y);
            wy_max = std::max(wy_max, c.y);
        }
    }

    const f32 pad = spread + 20.0f;
    return {static_cast<i32>(std::floor(wx_min - pad)),
        static_cast<i32>(std::floor(wy_min - pad)),
        static_cast<i32>(std::ceil(wx_max + pad)),
        static_cast<i32>(std::ceil(wy_max + pad))
    };
}

} // namespace chronon3d::renderer
