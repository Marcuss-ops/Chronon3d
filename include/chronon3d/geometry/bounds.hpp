#pragma once

#include <chronon3d/math/vec3.hpp>
#include <algorithm>
#include <limits>

namespace chronon3d {

struct AABB {
    Vec3 min{std::numeric_limits<f32>::max()};
    Vec3 max{std::numeric_limits<f32>::lowest()};

    constexpr AABB() = default;
    constexpr AABB(Vec3 min_pt, Vec3 max_pt) : min(min_pt), max(max_pt) {}

    static AABB from_points(const Vec3* points, usize count) {
        AABB bounds;
        for (usize i = 0; i < count; ++i) {
            bounds.merge(points[i]);
        }
        return bounds;
    }

    void merge(const Vec3& point) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);

        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }

    void merge(const AABB& other) {
        merge(other.min);
        merge(other.max);
    }

    [[nodiscard]] bool contains(const Vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    [[nodiscard]] bool intersects(const AABB& other) const {
        return (min.x <= other.max.x && max.x >= other.min.x) &&
               (min.y <= other.max.y && max.y >= other.min.y) &&
               (min.z <= other.max.z && max.z >= other.min.z);
    }

    [[nodiscard]] Vec3 center() const {
        return (min + max) * 0.5f;
    }

    [[nodiscard]] Vec3 extents() const {
        return (max - min) * 0.5f;
    }
};

} // namespace chronon3d
