#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/text_run.hpp>

namespace chronon3d {

class SoftwareRenderer;

namespace renderer {

// ── TextRunDrawParams — parameters for drawing a batched text run ───────

struct TextRunDrawParams {
    Framebuffer& fb;                              // target framebuffer
    const TextRunShape& shape;                     // batched text run with glyph states
    const Mat4& model_matrix;                      // world-space transform
    f32 opacity{1.0f};                            // overall layer opacity
    bool diagnostic_mode{false};                   // enable timing logs
};

/// Draw a batched text run with per-glyph transforms.
///
/// Unlike SoftwareTextProcessor (which rasterizes the full text string to
/// a single BLImage and composites it), this processor draws each glyph
/// individually with its own transform matrix, opacity, fill, and stroke.
/// All glyphs are rendered to a single intermediate BLImage for batching,
/// then composited onto the target framebuffer.
///
/// Per-glyph matrix order (After Effects convention):
///   T(layout_position) × T(position) × T(anchor) × R(rotation) × Skew × S(scale) × T(-anchor)
///
/// @param renderer  The software renderer (for font resources, diagnostics).
/// @param params    Draw parameters (target fb, shape, matrix, opacity).
/// @return true if at least one glyph was drawn, false on failure.
[[nodiscard]] bool draw_text_run(
    SoftwareRenderer& renderer,
    TextRunDrawParams& params
);

/// Compute the world-space bounding box of a text run.
/// Uses glyph positions, blur, and shadow spread to compute a conservative box.
[[nodiscard]] raster::BBox compute_text_run_world_bbox(
    const TextRunShape& shape,
    const Mat4& model,
    f32 spread
);

// ═══════════════════════════════════════════════════════════════════════════
// bucket_radius_for_tier — 4-tier blur radius mapping
// ═══════════════════════════════════════════════════════════════════════════
//
// Maps a blur radius (px) to the integer box-blur radius used by
// `draw_text_run`'s per-layer blur pass.  Tiers: 0/1–4/5–8/9–16/>16
// → 0/2/7/13/20 (the 20 is a cap that prevents edge wash at extreme
// radii on high-DPI canvases).  Single source of truth: same function
// consumed by production code and tests.

namespace detail {

[[nodiscard]] inline int bucket_radius_for_tier(float r) {
    if (r <= 0.0f)  return 0;
    if (r <= 4.0f)  return 2;
    if (r <= 8.0f)  return 7;
    if (r <= 16.0f) return 13;
    return 20;
}

} // namespace detail

} // namespace renderer
} // namespace chronon3d
