#pragma once

#include <chronon3d/core/types.hpp>
#include <cmath>

namespace chronon3d {

enum class Easing {
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InExpo,
    OutExpo,
    InOutExpo,
    OutBack
};

namespace easing {

inline f32 apply(Easing type, f32 t) {
    switch (type) {
        case Easing::Linear: return t;
        case Easing::InQuad: return t * t;
        case Easing::OutQuad: return t * (2.0f - t);
        case Easing::InOutQuad: return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        case Easing::InCubic: return t * t * t;
        case Easing::OutCubic: return (--t) * t * t + 1.0f;
        case Easing::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
        case Easing::InExpo: return (t == 0.0f) ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
        case Easing::OutExpo: return (t == 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case Easing::InOutExpo:
            if (t == 0.0f || t == 1.0f) return t;
            if ((t *= 2.0f) < 1.0f) return 0.5f * std::pow(2.0f, 10.0f * (t - 1.0f));
            return 0.5f * (2.0f - std::pow(2.0f, -10.0f * (t - 1.0f)));
        case Easing::OutBack: {
            const f32 c1 = 1.70158f;
            const f32 c3 = c1 + 1.0f;
            return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
        }
        default: return t;
    }
}

} // namespace easing

} // namespace chronon3d
