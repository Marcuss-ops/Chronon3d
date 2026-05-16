#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/math/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::animation {

enum class Easing {
    Linear,
    Smoothstep,
    EaseOutCubic,
    EaseInOutCubic,
};

inline f32 lerp(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

inline f32 smoothstep(f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline f32 normalized_time(Frame frame, Frame duration) {
    const Frame span = std::max<Frame>(1, duration - 1);
    return smoothstep(static_cast<f32>(frame) / static_cast<f32>(span));
}

inline f32 easing_value(Easing easing, f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseOutCubic: {
        const f32 u = 1.0f - t;
        return 1.0f - u * u * u;
    }
    case Easing::EaseInOutCubic:
        return (t < 0.5f)
            ? 4.0f * t * t * t
            : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case Easing::Smoothstep:
    default:
        return smoothstep(t);
    }
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, f32 t) {
    return a + (b - a) * t;
}

} // namespace chronon3d::animation
