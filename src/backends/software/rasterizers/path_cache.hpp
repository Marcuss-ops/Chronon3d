#pragma once

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace chronon3d::renderer {

using CacheKey = uint64_t;

struct PathContour {
    std::vector<Vec2> points;
    bool closed{false};
};

CacheKey path_cache_hash_combine(CacheKey seed, CacheKey value);
CacheKey path_cache_hash_path(const PathShape& path);

template <typename T>
CacheKey path_cache_hash_value(const T& value) {
    return static_cast<CacheKey>(std::hash<T>{}(value));
}

std::vector<PathContour> flatten_to_contours(const PathShape& path);

// ---------------------------------------------------------------------------
// PathFlattenCache — LRU cache for flattened path contours
// Speeds up repeated path flattening for paths that don't change between
// frames (e.g. static shapes, grid backgrounds).
// ---------------------------------------------------------------------------
inline cache::LruCache<CacheKey, std::shared_ptr<const std::vector<PathContour>>>&
path_flatten_cache() {
    // 16 shards, 64 MB budget, weight = total points * sizeof(Vec2)
    static cache::LruCache<CacheKey, std::shared_ptr<const std::vector<PathContour>>>
        cache(64ULL * 1024 * 1024, 16);
    return cache;
}

/// Cached version of flatten_to_contours — checks the LRU cache first.
inline std::vector<PathContour> flatten_path_cached(const PathShape& path) {
    const CacheKey hash = path_cache_hash_path(path);
    auto& cache = path_flatten_cache();

    // Check cache
    if (auto cached = cache.get(hash)) {
        return **cached;
    }

    // Flatten and cache
    auto contours = std::make_shared<const std::vector<PathContour>>(flatten_to_contours(path));

    // Weight = total points * sizeof(Vec2) + overhead per contour
    size_t weight = 0;
    for (const auto& c : *contours) {
        weight += c.points.size() * sizeof(Vec2) + sizeof(bool);
    }
    weight += sizeof(std::vector<PathContour>);

    cache.put(hash, contours, weight);
    return *contours;
}

} // namespace chronon3d::renderer
