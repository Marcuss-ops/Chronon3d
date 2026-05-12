#pragma once

#include <chronon3d/math/math_base.hpp>

namespace chronon3d {

// We keep some static helpers for compatibility with existing code
namespace math {
    inline Mat4 translate(const Vec3& v) { return glm::translate(Mat4(1.0f), v); }
    inline Mat4 scale(const Vec3& v) { return glm::scale(Mat4(1.0f), v); }
    inline Mat4 rotate(const Quat& q) { return glm::toMat4(q); }
    inline Mat4 perspective(f32 fov_rad, f32 aspect, f32 near, f32 far) { 
        return glm::perspective(fov_rad, aspect, near, far); 
    }
    inline Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
        return glm::lookAt(eye, center, up);
    }
}

// For compatibility with code that calls Mat4::translate(...)
// We can't easily add static methods to a type alias 'using Mat4 = glm::mat4'.
// So we might need a small wrapper if we want to keep that exact syntax.
// However, it's better to update the call sites to use chronon3d::math::translate.

} // namespace chronon3d
