#pragma once

#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/quat.hpp>
#include <chronon3d/math/vec3.hpp>
#include <glm/gtx/quaternion.hpp>

namespace chronon3d::math {

inline Quat camera_rotation_quat(const Vec3& euler_degrees) {
    return from_euler(euler_degrees);
}

inline Vec3 camera_rotation_euler(const Quat& rotation) {
    const Vec3 radians = glm::eulerAngles(rotation);
    return glm::degrees(radians);
}

inline Mat4 camera_view_matrix(const Vec3& position, const Quat& rotation) {
    return glm::inverse(translate(position) * rotate(rotation));
}

inline Mat4 camera_view_matrix(const Vec3& position, const Vec3& euler_degrees) {
    return camera_view_matrix(position, camera_rotation_quat(euler_degrees));
}

inline Mat4 camera_view_matrix(const Vec3& position, const Quat& rotation,
                               const Vec3& point_of_interest) {
    if (glm::length(point_of_interest - position) > 0.001f) {
        return math::look_at(position, point_of_interest, Vec3{0.0f, 1.0f, 0.0f});
    }
    return camera_view_matrix(position, rotation);
}

} // namespace chronon3d::math
