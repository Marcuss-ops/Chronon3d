#include "path_geometry.hpp"

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace chronon3d::renderer {

namespace {

constexpr std::size_t kFlattenCapacityBytes = 64ULL * 1024 * 1024;
constexpr int kFlattenShardCount = 16;

// ── Single, source-of-truth LRU cache ────────────────────────────────
// Replaces the two duplicate caches that previously lived in
// path_cache.hpp (function-local static) AND path_cache.cpp (global).
// Both code paths produced separate LRUState and were silently
// halving the effective cache budget.
cache::LruCache<PathCacheKey, detail::SharedContours> g_flatten_cache(
    kFlattenCapacityBytes, kFlattenShardCount);

} // namespace

PathCacheKey path_cache_hash_combine(PathCacheKey seed, PathCacheKey value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

PathCacheKey path_cache_hash_path(const PathShape& path) {
    PathCacheKey seed = path_cache_hash_value(path.closed);
    seed = path_cache_hash_combine(seed, path_cache_hash_value(path.commands.size()));
    for (const auto& cmd : path.commands) {
        seed = path_cache_hash_combine(seed, path_cache_hash_value(static_cast<int>(cmd.type)));
        seed = path_cache_hash_combine(seed, path_cache_hash_value(cmd.p0.x));
        seed = path_cache_hash_combine(seed, path_cache_hash_value(cmd.p0.y));
        seed = path_cache_hash_combine(seed, path_cache_hash_value(cmd.p1.x));
        seed = path_cache_hash_combine(seed, path_cache_hash_value(cmd.p1.y));
        seed = path_cache_hash_combine(seed, path_cache_hash_value(cmd.p2.x));
        seed = path_cache_hash_combine(seed, path_cache_hash_value(cmd.p2.y));
    }
    return seed;
}

detail::SharedContours flatten_path_geometry_cached(const PathShape& path) {
    const PathCacheKey key = path_cache_hash_path(path);

    if (auto cached = g_flatten_cache.get(key)) {
        if (auto ptr = *cached) return ptr;
    }

    std::vector<PathContour> contours;
    const auto subpaths = math::flatten_path(path);

    contours.reserve(subpaths.size());
    for (auto points : subpaths) {
        PathContour contour;
        contour.points = std::move(points);
        contour.closed = path.closed;
        if (!contour.points.empty()) {
            const bool already_closed =
                contour.points.size() > 2 &&
                glm::length(contour.points.front() - contour.points.back()) < 1e-4f;
            if (path.closed && !already_closed) {
                contour.points.push_back(contour.points.front());
            }
            contour.closed = contour.closed || already_closed;
        }
        contours.push_back(std::move(contour));
    }

    std::size_t weight = 0;
    for (const auto& c : contours) {
        weight += c.points.size() * sizeof(Vec2) + sizeof(PathContour);
    }
    if (weight == 0) weight = 1;

    auto shared = std::make_shared<const std::vector<PathContour>>(std::move(contours));
    g_flatten_cache.put(key, shared, weight);
    return shared;
}

namespace detail {

raster::BBox compute_path_world_bbox(const PathShape& path, const Mat4& model,
                                      f32 spread) {
    auto contours = flatten_path_geometry_cached(path);
    if (!contours || contours->empty()) return {0, 0, 0, 0};

    const f32 padding = path.stroke.width * 0.5f + spread;
    f32 min_x = 1e10f, min_y = 1e10f;
    f32 max_x = -1e10f, max_y = -1e10f;
    bool empty = true;

    for (const auto& contour : *contours) {
        for (const auto& p : contour.points) {
            const Vec2 corners[4] = {
                p + Vec2(-padding, -padding),
                p + Vec2( padding, -padding),
                p + Vec2( padding,  padding),
                p + Vec2(-padding,  padding),
            };
            for (const auto& c : corners) {
                const Vec4 p_world = model * Vec4(c.x, c.y, 0.0f, 1.0f);
                const f32 w = std::abs(p_world.w) > 1e-7f ? p_world.w : 1.0f;
                const f32 px = p_world.x / w;
                const f32 py = p_world.y / w;
                min_x = std::min(min_x, px);
                max_x = std::max(max_x, px);
                min_y = std::min(min_y, py);
                max_y = std::max(max_y, py);
                empty = false;
            }
        }
    }

    if (empty) return {0, 0, 0, 0};

    return {
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)) + 1,
        static_cast<i32>(std::ceil(max_y)) + 1,
    };
}

bool hit_test_path(const PathShape& path, Vec2 local_point, f32 spread) {
    auto contours = flatten_path_geometry_cached(path);
    if (!contours || contours->empty()) return false;

    const f32 radius_sq =
        std::pow(path.stroke.width * 0.5f + spread, 2.0f);

    for (const auto& contour : *contours) {
        const auto& pts = contour.points;
        if (pts.size() < 2) continue;

        for (std::size_t i = 1; i < pts.size(); ++i) {
            const Vec2 ab = pts[i] - pts[i - 1];
            const f32 ab_len_sq = glm::dot(ab, ab);
            if (ab_len_sq < 1e-6f) {
                const Vec2 diff = local_point - pts[i - 1];
                if (glm::dot(diff, diff) <= radius_sq) return true;
                continue;
            }
            const f32 t = std::clamp(
                glm::dot(local_point - pts[i - 1], ab) / ab_len_sq,
                0.0f, 1.0f);
            const Vec2 closest = pts[i - 1] + ab * t;
            const Vec2 diff = local_point - closest;
            if (glm::dot(diff, diff) <= radius_sq) return true;
        }
    }
    return false;
}

} // namespace detail

} // namespace chronon3d::renderer
