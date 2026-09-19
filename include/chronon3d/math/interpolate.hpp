#pragma once

#include <chronon3d/math/glm_types.hpp>

namespace chronon3d::math {

// Small, clamped interpolation primitives shared by camera, transition and
// effect code. Animation easing curves remain owned by animation/easing.
[[nodiscard]] constexpr f32 clamp01(f32 value) noexcept {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

[[nodiscard]] constexpr f32 smoothstep(f32 value) noexcept {
    const f32 t = clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] constexpr f32 lerp(f32 from, f32 to, f32 value) noexcept {
    return from + (to - from) * clamp01(value);
}

[[nodiscard]] inline Vec3 lerp(const Vec3& from, const Vec3& to, f32 value) noexcept {
    return from + (to - from) * clamp01(value);
}

} // namespace chronon3d::math
