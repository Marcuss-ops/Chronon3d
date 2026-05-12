#pragma once

#include <chronon3d/core/types.hpp>
#include <cmath>

namespace chronon3d {

enum class Easing {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic
};

namespace easing {

inline f32 apply(f32 t, Easing type) {
    switch (type) {
        case Easing::Linear:
            return t;
        case Easing::EaseInQuad:
            return t * t;
        case Easing::EaseOutQuad:
            return t * (2.0f - t);
        case Easing::EaseInOutQuad:
            return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        case Easing::EaseInCubic:
            return t * t * t;
        case Easing::EaseOutCubic:
            return (--t) * t * t + 1.0f;
        case Easing::EaseInOutCubic:
            return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
        default:
            return t;
    }
}

} // namespace easing

} // namespace chronon3d
