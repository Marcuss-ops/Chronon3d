#pragma once

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/scene/shape.hpp>
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

} // namespace chronon3d::renderer
