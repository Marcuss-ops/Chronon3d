#pragma once

#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/quat.hpp>
#include <chronon3d/math/mat4.hpp>

namespace chronon3d {

struct Transform {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // Identity
    Vec3 scale{1.0f, 1.0f, 1.0f};

    constexpr Transform() = default;
    constexpr Transform(Vec3 p, Quat r = Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3 s = Vec3(1.0f))
        : position(p), rotation(r), scale(s) {}

    [[nodiscard]] Mat4 to_matrix() const {
        return math::translate(position) * math::rotate(rotation) * math::scale(scale);
    }
};

} // namespace chronon3d
