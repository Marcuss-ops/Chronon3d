#pragma once

#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/quat.hpp>
#include <chronon3d/math/mat4.hpp>

namespace chronon3d {

struct Transform {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // Identity
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 anchor{0.0f, 0.0f, 0.0f};
    f32 opacity{1.0f};

    constexpr Transform() = default;
    constexpr Transform(Vec3 p, Quat r = Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3 s = Vec3(1.0f), Vec3 a = Vec3(0.0f), f32 o = 1.0f)
        : position(p), rotation(r), scale(s), anchor(a), opacity(o) {}

    [[nodiscard]] Mat4 to_matrix() const {
        // Matrix = Translate(position) * Rotate(rotation) * Scale(scale) * Translate(-anchor)
        return math::translate(position) * math::rotate(rotation) * math::scale(scale) * math::translate(-anchor);
    }

    [[nodiscard]] bool is_identity_2d() const {
        return rotation.w == 1.0f && scale.x == 1.0f && scale.y == 1.0f && anchor.x == 0.0f && anchor.y == 0.0f;
    }
};

struct RenderState {
    Mat4 matrix;
    f32 opacity{1.0f};
};

inline RenderState combine(const RenderState& parent, const Transform& child) {
    return RenderState{
        .matrix = parent.matrix * child.to_matrix(),
        .opacity = parent.opacity * child.opacity
    };
}

} // namespace chronon3d
