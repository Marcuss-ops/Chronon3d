#pragma once

#include <chronon3d/math/math_base.hpp>

namespace chronon3d {

// Extension methods for compatibility with old API
namespace math {
    inline f32 dot(const Vec2& a, const Vec2& b) { return glm::dot(a, b); }
    inline f32 length(const Vec2& v) { return glm::length(v); }
    inline Vec2 normalize(const Vec2& v) { return glm::normalize(v); }
}

} // namespace chronon3d
