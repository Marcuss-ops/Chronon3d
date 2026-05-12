#pragma once

#include <chronon3d/math/vec3.hpp>

namespace chronon3d {

struct Ray {
    Vec3 origin{0.0f, 0.0f, 0.0f};
    Vec3 direction{0.0f, 0.0f, 1.0f};

    constexpr Ray() = default;
    Ray(Vec3 origin, Vec3 direction) 
        : origin(origin), direction(direction.normalized()) {}

    [[nodiscard]] constexpr Vec3 point_at(f32 t) const {
        return origin + direction * t;
    }
};

} // namespace chronon3d
