#pragma once

#include <chronon3d/core/types/types.hpp>

// GLM Configuration
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace chronon3d::math {

inline f32 dot(const Vec2& a, const Vec2& b) { return glm::dot(a, b); }
inline f32 length(const Vec2& v) { return glm::length(v); }
inline Vec2 normalize(const Vec2& v) { return glm::normalize(v); }

inline f32 dot(const Vec3& a, const Vec3& b) { return glm::dot(a, b); }
inline Vec3 cross(const Vec3& a, const Vec3& b) { return glm::cross(a, b); }
inline f32 length(const Vec3& v) { return glm::length(v); }
inline Vec3 normalize(const Vec3& v) { return glm::normalize(v); }

inline Quat slerp(const Quat& a, const Quat& b, f32 t) { return glm::slerp(a, b, t); }
inline Quat lerp(const Quat& a, const Quat& b, f32 t) { return glm::lerp(a, b, t); }
inline f32 dot(const Quat& a, const Quat& b) { return glm::dot(a, b); }
inline Quat normalize(const Quat& q) { return glm::normalize(q); }

inline Mat4 translate(const Vec3& v) { return glm::translate(Mat4(1.0f), v); }
inline Mat4 scale(const Vec3& v) { return glm::scale(Mat4(1.0f), v); }
inline Mat4 rotate(const Quat& q) { return glm::toMat4(q); }
inline Mat4 perspective(f32 fov_rad, f32 aspect, f32 near, f32 far) {
    return glm::perspective(fov_rad, aspect, near, far);
}
inline Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
    return glm::lookAt(eye, center, up);
}

inline Quat from_euler(const Vec3& euler_degrees) {
    return Quat(glm::radians(euler_degrees));
}

} // namespace chronon3d::math
