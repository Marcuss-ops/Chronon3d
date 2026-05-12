#pragma once

#include <chronon3d/math/math_base.hpp>

namespace chronon3d {

// Extension methods for compatibility with old API
namespace math {
    inline f32 dot(const Vec3& a, const Vec3& b) { return glm::dot(a, b); }
    inline Vec3 cross(const Vec3& a, const Vec3& b) { return glm::cross(a, b); }
    inline f32 length(const Vec3& v) { return glm::length(v); }
    inline Vec3 normalize(const Vec3& v) { return glm::normalize(v); }
}

} // namespace chronon3d
