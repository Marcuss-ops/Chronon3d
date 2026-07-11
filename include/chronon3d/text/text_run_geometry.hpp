#pragma once

// ---------------------------------------------------------------------------
// text_run_geometry.hpp — Backend-independent text run geometry utilities
//
// Extracted from backends/software/text_run_processor.hpp so that the render
// graph (TextRunNode, MultiSourceNode) can compute bounding boxes without
// pulling in the software backend.
// ---------------------------------------------------------------------------

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/media/media_placement.hpp>   // chronon3d::Rect
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <optional>

namespace chronon3d::renderer {

/// Local-space visual bounds of a text run (no model transform applied).
/// Returned by compute_text_run_visual_bounds().
struct TextRunLocalBounds {
    f32 min_x;
    f32 min_y;
    f32 max_x;
    f32 max_y;
};

/// Convert local bounds to the project's canonical Rect.
[[nodiscard]] inline Rect to_rect(const TextRunLocalBounds& b) noexcept {
    return Rect{{b.min_x, b.min_y},
                {b.max_x - b.min_x, b.max_y - b.min_y}};
}

/// Compute the local-space visual bounds of a text run.
///
/// Walks both active and crossfade glyph vectors, accumulating a
/// conservative bounding box that accounts for:
///   - glyph layout positions + animated offsets
///   - per-glyph blur and stroke width
///   - 2.5D shear estimates (rotation.x/y)
///   - scale.z expansion
///   - per-glyph advance from `placed.glyphs[i].advance_x`
///     (TICKET-TEXT-CLIP-ASCENT: was a hardcoded 12 px approximation
///      which clipped wider glyphs on the right edge of the scratch
///      surface — the "touches right edge" symptom seen in the bug
///      dashboard PNG)
///   - baseline-anchored ascent/descent from `placed.ascent/descent`
///     (TICKET-TEXT-CLIP-ASCENT: was `placed.total_height` combined
///      with `gy - pad`, which anchored the bbox to the baseline
///      instead of the top of the glyph ink and clipped ~80% of
///      ascender extents — the "19 px sliver in 1080-row canvas"
///      symptom seen in the bug dashboard PNG)
///
/// Returns std::nullopt when both active and crossfade sides are empty.
/// This is the SINGLE canonical per-glyph bbox accumulator — used by
/// compute_text_run_world_bbox (adds model transform + spread) and by
/// the software rasterizer's prepare stage (uses local-space directly).
[[nodiscard]] std::optional<TextRunLocalBounds> compute_text_run_visual_bounds(
    const TextRunShape& shape
);

/// Compute the world-space bounding box of a text run.
///
/// Delegates to compute_text_run_visual_bounds() for local-space bounds,
/// then transforms the four corners by @p model and pads by @p spread.
///
/// This function is pure geometry — it has no dependency on any rendering
/// backend and can be used from the render graph, preflight, or tests.
[[nodiscard]] raster::BBox compute_text_run_world_bbox(
    const TextRunShape& shape,
    const Mat4& model,
    f32 spread
);

} // namespace chronon3d::renderer
