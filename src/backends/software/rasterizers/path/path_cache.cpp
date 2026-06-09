#include "path_cache.hpp"
#include <chronon3d/math/path_utils.hpp>
#include <glm/glm.hpp>

namespace chronon3d::renderer {

namespace {

constexpr size_t kFlattenCacheCapacity = 64 * 1024 * 1024;
constexpr int kFlattenCacheMaxObjects = 16;    cache::LruCache<CacheKey, std::shared_ptr<const std::vector<PathContour>>> g_flatten_cache(
    kFlattenCacheCapacity, kFlattenCacheMaxObjects);

} // namespace

CacheKey path_cache_hash_combine(CacheKey seed, CacheKey value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

CacheKey path_cache_hash_path(const PathShape& path) {
    CacheKey seed = path_cache_hash_value(path.closed);
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

std::vector<PathContour> flatten_to_contours(const PathShape& path) {
    const CacheKey key = path_cache_hash_path(path);
    auto cached = g_flatten_cache.get(key);
    if (cached && *cached) {
        return **cached;
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

    size_t weight = 0;
    for (const auto& c : contours) {
        weight += c.points.size() * sizeof(Vec2) + sizeof(PathContour);
    }
    if (weight == 0) weight = 1;

    auto shared_contours = std::make_shared<const std::vector<PathContour>>(contours);
    g_flatten_cache.put(key, shared_contours, weight);

    return contours;
}

std::vector<PathContour> flatten_path(const PathShape& path) {
    return flatten_to_contours(path);
}

} // namespace chronon3d::renderer
