#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// ──────────────────────────────────────────────────────────────────────
// PR2: path geometry — single source-of-truth for caching, world bbox,
// and local-space hit-testing.  Replaces the duplicates that were
// scattered between path_cache.{hpp,cpp},
// path_rasterizer.cpp::compute_path_bbox (cache-keywise) and
// PathProcessor::{compute_world_bbox, hit_test} (logic-wise).
// ──────────────────────────────────────────────────────────────────────

namespace chronon3d::renderer {

// Output of the path flattener: one polyline (closed or open) per
// sub-path.  Moved here from path_cache.hpp so all geometry-related
// types live in one place.
struct PathContour {
    std::vector<Vec2> points;
    bool closed{false};
};

// Hash key shared by the LRU cache (was `CacheKey` in path_cache.hpp).
using PathCacheKey = std::uint64_t;
PathCacheKey path_cache_hash_combine(PathCacheKey seed, PathCacheKey value);
PathCacheKey path_cache_hash_path(const PathShape& path);

template <typename T>
PathCacheKey path_cache_hash_value(const T& value) {
    return static_cast<PathCacheKey>(std::hash<T>{}(value));
}

// ── Detail-private geometry symbols ─────────────────────────────────────
//
// These types and functions are implementation details of the
// renderer (cache layout, perspective bbox formula, hit-test internal
// representation).  Keep them under `detail::` so exposing
// `path_geometry.hpp` through any future public ABI surface does not
// pin the layout of `SharedContours` or commit us to a signature that
// may change with PR3+ (Blend2D delegation, arc-length cache, etc.).
//
// Public API kept: `path_cache_hash_*` and the entry-point
// `flatten_path_geometry_cached` (the only function callers actually
// invoke by name).
namespace detail {

using SharedContours = std::shared_ptr<const std::vector<PathContour>>;

// Perspective-aware world bbox used by the shape-processor dispatch.
// Replaces the manual flatten+bbox in PathProcessor::compute_world_bbox.
//
// The function projects four corner paddings of every contour vertex
// using `model * Vec4(x, y, 0, 1)` and divides by `w` when |w| > 1e-7
// (otherwise falls back to affine).  This is the single-shape-processor
// version; for legacy affine stroke clipping use
// `compute_path_bbox` in path_rasterizer.hpp.
raster::BBox compute_path_world_bbox(const PathShape& path, const Mat4& model,
                                      f32 spread = 0.0f);

// Local-space hit test against the flattened stroke geometry.
bool hit_test_path(const PathShape& path, Vec2 local_point, f32 spread);

} // namespace detail

// Public entry-point: cached flatten.  Returns a `detail::SharedContours`
// so the caller never names the type — they just iterate `*contours`.
detail::SharedContours flatten_path_geometry_cached(const PathShape& path);

} // namespace chronon3d::renderer
