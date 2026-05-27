#pragma once

#include <chronon3d/core/types/types.hpp>

// GLM Configuration
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>

namespace chronon3d {
    // Basic types
    using Vec2 = glm::vec2;
    using Vec3 = glm::vec3;
    using Vec4 = glm::vec4;
    using Mat4 = glm::mat4;
    using Quat = glm::quat;
}

namespace chronon3d::math {

inline Mat4 translate(const Vec3& v) { return glm::translate(Mat4(1.0f), v); }
inline Mat4 scale(const Vec3& v) { return glm::scale(Mat4(1.0f), v); }
inline Mat4 rotate(const Quat& q) { return glm::toMat4(q); }
inline Quat from_euler(const Vec3& euler_degrees) { return Quat(glm::radians(euler_degrees)); }

inline Mat4 perspective(f32 fov_rad, f32 aspect, f32 near, f32 far) {
    return glm::perspective(fov_rad, aspect, near, far);
}
inline Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
    return glm::lookAt(eye, center, up);
}

} // namespace chronon3d::math
