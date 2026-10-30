#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

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
    const Mat4 m = glm::toMat4(glm::normalize(rotation));
    const float sinY = std::clamp(-m[0][2], -1.0f, 1.0f);
    const float y = std::asin(sinY);
    const float cosY = std::cos(y);
    float x = 0.0f;
    float z = 0.0f;
    if (std::abs(cosY) > 1e-6f) {
        x = std::atan2(m[1][2], m[2][2]);
        z = std::atan2(m[0][1], m[0][0]);
    } else {
        x = std::atan2(sinY * m[1][0], m[1][1]);
    }
    return glm::degrees(Vec3{x, y, z});
}

inline Mat4 camera_view_matrix(const Vec3& position, const Quat& rotation) {
    return glm::inverse(glm::translate(Mat4(1.0f), position) * glm::toMat4(rotation));
}

inline Mat4 camera_view_matrix(const Vec3& position, const Vec3& euler_degrees) {
    return camera_view_matrix(position, camera_rotation_quat(euler_degrees));
}

} // namespace chronon3d::math
} // namespace chronon3d
