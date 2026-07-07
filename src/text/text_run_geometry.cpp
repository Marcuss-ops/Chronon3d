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
//   - approximate glyph advance (12px)
//   - placed.total_height for baseline-to-bottom extent
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
            for (const auto& g : glyphs) {
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
                const float scale_extra =
                    std::abs(static_cast<float>(g.scale.z) - 1.0f)
                    * std::abs(static_cast<float>(g.layout_position.y));
                const float pad = g.blur + g.stroke_width + 8.0f
                    + shear_x_extra + shear_y_extra + scale_extra;
                min_x = std::min(min_x, gx - pad);
                max_x = std::max(
                    max_x, gx + pad + 12.0f);    // approximate glyph advance
                min_y = std::min(min_y, gy - pad);
                max_y = std::max(max_y, gy + pad + placed.total_height);
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
