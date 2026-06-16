#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <glm/gtx/quaternion.hpp>

namespace chronon3d {

/// Camera pose utility: position + orientation quaternion with direction helpers.
struct CameraPose {
    Vec3 position;
    Quat orientation;

    CameraPose() = default;
    CameraPose(const Vec3& pos, const Quat& orient)
        : position(pos), orientation(orient) {}

    Vec3 forward() const { return orientation * Vec3{0.0f, 0.0f, -1.0f}; }
    Vec3 right()   const { return orientation * Vec3{1.0f, 0.0f, 0.0f};  }
    Vec3 up()      const { return orientation * Vec3{0.0f, 1.0f, 0.0f};  }
};

namespace math {

inline Quat camera_rotation_quat(const Vec3& euler_degrees) {
    return glm::quat(glm::radians(euler_degrees));
}

inline Vec3 camera_rotation_euler(const Quat& rotation) {
    const Vec3 radians = glm::eulerAngles(rotation);
    return glm::degrees(radians);
}

inline Mat4 camera_view_matrix(const Vec3& position, const Quat& rotation) {
    return glm::inverse(glm::translate(Mat4(1.0f), position) * glm::toMat4(rotation));
}

inline Mat4 camera_view_matrix(const Vec3& position, const Vec3& euler_degrees) {
    return camera_view_matrix(position, camera_rotation_quat(euler_degrees));
}

inline Mat4 camera_view_matrix(const Vec3& position, const Quat& rotation,
                               const Vec3& point_of_interest) {
    if (glm::length(point_of_interest - position) > 0.001f) {
        return glm::lookAt(position, point_of_interest, Vec3{0.0f, 1.0f, 0.0f});
    }
    return camera_view_matrix(position, rotation);
}

} // namespace chronon3d::math
} // namespace chronon3d
