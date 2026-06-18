#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include "path_cache.hpp"
#include <cstddef>
#include <vector>

namespace chronon3d::renderer {

f32 segment_coverage(Vec2 p, Vec2 a, Vec2 b, f32 radius);

std::vector<Vec2> trim_polyline_points(const std::vector<Vec2>& points, bool closed, f32 start_t, f32 end_t);

std::vector<std::vector<Vec2>> dash_polyline_points(const std::vector<Vec2>& points, bool closed, const std::vector<f32>& dash_array, f32 dash_offset);

// ── Prepared stroke contours (PR1 hot-loop precompute) ───────────────
// Replaces per-pixel calls to trim_polyline_points + dash_polyline_points
// inside the rasterisation loop.  Geometry is now resolved once per
// draw_path call rather than once per (x, y, contour).
//
// The data types live in `detail` so they do not leak through the public
// ABI of `include/chronon3d/backends/software/rasterizers/path_rasterizer.hpp`
// once PR2-Promote promotes the rasterizer to a public header.  Only
// `prepare_stroke_contours` is exported from the main namespace.
namespace detail {

struct PreparedSubPath {
    std::vector<Vec2> points;
    bool repeated_start{false};  // closed && front≈back && size > 2
    std::size_t unique_count{0}; // effective vertex count for join loop
};

struct PreparedStrokeContour {
    std::vector<PreparedSubPath> visible_subpaths;
    bool source_closed{false};   // original contour.closed (used for caps/joins)
};

} // namespace detail

// Applies PathStroke.trim → PathStroke.dash_array once for the whole
// rasterisation.  Preserve identical semantics to the legacy per-pixel
// path, including the `pts = trimmed.empty() ? contour.points : trimmed`
// fallback for invalid trim ranges.
std::vector<detail::PreparedStrokeContour> prepare_stroke_contours(
    const std::vector<PathContour>& screen_contours,
    const PathStroke& stroke);

} // namespace chronon3d::renderer
