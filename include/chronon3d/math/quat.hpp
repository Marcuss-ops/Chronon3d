#pragma once

#include <chronon3d/math/math_base.hpp>

namespace chronon3d {

// Extension methods for compatibility with old API
namespace math {
    inline Quat slerp(const Quat& a, const Quat& b, f32 t) { return glm::slerp(a, b, t); }
    inline Quat lerp(const Quat& a, const Quat& b, f32 t) { return glm::lerp(a, b, t); }
    inline f32 dot(const Quat& a, const Quat& b) { return glm::dot(a, b); }
    inline Quat normalize(const Quat& q) { return glm::normalize(q); }
}

// Global lerp for compatibility with AnimatedValue template
inline Quat lerp(const Quat& a, const Quat& b, f32 t) {
    return glm::slerp(a, b, t); // Slerp is usually better for quaternions
}

} // namespace chronon3d
