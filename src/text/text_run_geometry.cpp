// ---------------------------------------------------------------------------
// text_run_geometry.cpp — Backend-independent text run bbox computation
//
// Extracted from backends/software/processors/text_run/text_run_processor.cpp
// so the render graph can compute bounding boxes without linking the software
// backend.
// ---------------------------------------------------------------------------

#include <chronon3d/text/text_run_geometry.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace chronon3d::renderer {

// ═══════════════════════════════════════════════════════════════════════════
// compute_text_run_world_bbox
// ═══════════════════════════════════════════════════════════════════════════
//
// Conservative approximation.  Uses layout positions + position offsets,
// per-glyph rotation.x/y shear estimates, scale.z expansion, blur,
// stroke width, and spread padding.  Transforms the four corners of the
// local-space bbox by the model matrix to produce a world-space bbox.

raster::BBox compute_text_run_world_bbox(
    const TextRunShape& shape,
    const Mat4& model,
    f32 spread
) {
    const bool has_active =
        shape.layout != nullptr && !shape.glyphs.empty();
    const bool has_crossfade =
        shape.crossfade_layout != nullptr && !shape.crossfade_glyphs.empty();

    // Only return zero when BOTH sides are empty.  A single-side
    // edge case (e.g. active->utf8 is empty post-keyframe while
    // crossfade_from still has content) still shows the crossfade
    // glyphs during the brief residue frame, so we keep the bbox
    // computation alive for it.
    if (!has_active && !has_crossfade) {
        return {0, 0, 0, 0};
    }

    float min_x = 1e10f, max_x = -1e10f;
    float min_y = 1e10f, max_y = -1e10f;

    // Per-glyph bbox accumulator.  Walks one glyph vector against
    // one PlacedGlyphRun and widens the running local-space bbox.
    // Captured (by reference) so the active + crossfade iterations
    // below share the same min/max accumulators; the world-space
    // transform later operates on the union.
    auto accumulate_for_layout =
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
                    * static_cast<float>(g.layout_position.y);
                const float shear_y_extra = std::abs(std::tan(
                    std::clamp(
                        static_cast<float>(g.rotation.x)
                            * (3.14159265f / 180.0f),
                        -1.5607f, 1.5607f)))
                    * static_cast<float>(g.layout_position.x);
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
        accumulate_for_layout(shape.glyphs, shape.layout->placed);
    }
    if (has_crossfade) {
        accumulate_for_layout(shape.crossfade_glyphs,
                              shape.crossfade_layout->placed);
    }

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
