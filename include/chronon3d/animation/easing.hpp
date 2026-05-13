#pragma once

#include <chronon3d/core/types.hpp>
#include <glm/gtc/constants.hpp>
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
    InSine,
    OutSine,
    InOutSine,
    InBack,
    OutBack,
    InOutBack,
    InElastic,
    OutElastic,
    InOutElastic,
    InBounce,
    OutBounce,
    InOutBounce,
    Hold
};

using Ease = Easing;

namespace easing {

inline f32 apply(Easing type, f32 t) {
    switch (type) {
        case Easing::Linear: return t;
        case Easing::InQuad: return t * t;
        case Easing::OutQuad: return t * (2.0f - t);
        case Easing::InOutQuad: return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        case Easing::InCubic: return t * t * t;
        case Easing::OutCubic: return (t - 1.0f) * (t - 1.0f) * (t - 1.0f) + 1.0f;
        case Easing::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
        case Easing::InExpo: return (t == 0.0f) ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
        case Easing::OutExpo: return (t == 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case Easing::InOutExpo:
            if (t == 0.0f || t == 1.0f) return t;
            if ((t *= 2.0f) < 1.0f) return 0.5f * std::pow(2.0f, 10.0f * (t - 1.0f));
            return 0.5f * (2.0f - std::pow(2.0f, -10.0f * (t - 1.0f)));
        case Easing::InSine: return 1.0f - std::cos(t * glm::pi<f32>() * 0.5f);
        case Easing::OutSine: return std::sin(t * glm::pi<f32>() * 0.5f);
        case Easing::InOutSine: return -(std::cos(glm::pi<f32>() * t) - 1.0f) * 0.5f;
        case Easing::InBack: {
            const f32 c1 = 1.70158f;
            const f32 c3 = c1 + 1.0f;
            return c3 * t * t * t - c1 * t * t;
        }
        case Easing::OutBack: {
            const f32 c1 = 1.70158f;
            const f32 c3 = c1 + 1.0f;
            return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
        }
        case Easing::InOutBack: {
            const f32 c1 = 1.70158f;
            const f32 c2 = c1 * 1.525f;
            return t < 0.5f
                ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
                : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
        }
        case Easing::InElastic: {
            const f32 c4 = (2.0f * glm::pi<f32>()) / 3.0f;
            return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f
                : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
        }
        case Easing::OutElastic: {
            const f32 c4 = (2.0f * glm::pi<f32>()) / 3.0f;
            return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f
                : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
        }
        case Easing::InOutElastic: {
            const f32 c5 = (2.0f * glm::pi<f32>()) / 4.5f;
            return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f
                : t < 0.5f ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f
                : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f + 1.0f;
        }
        case Easing::OutBounce: {
            const f32 n1 = 7.5625f;
            const f32 d1 = 2.75f;
            if (t < 1.0f / d1) return n1 * t * t;
            else if (t < 2.0f / d1) return n1 * (t -= 1.5f / d1) * t + 0.75f;
            else if (t < 2.5f / d1) return n1 * (t -= 2.25f / d1) * t + 0.9375f;
            else return n1 * (t -= 2.625f / d1) * t + 0.984375f;
        }
        case Easing::InBounce: {
            // OutBounce on (1-t), then flipped
            f32 t_inv = 1.0f - t;
            const f32 n1 = 7.5625f;
            const f32 d1 = 2.75f;
            f32 res;
            if (t_inv < 1.0f / d1) res = n1 * t_inv * t_inv;
            else if (t_inv < 2.0f / d1) res = n1 * (t_inv -= 1.5f / d1) * t_inv + 0.75f;
            else if (t_inv < 2.5f / d1) res = n1 * (t_inv -= 2.25f / d1) * t_inv + 0.9375f;
            else res = n1 * (t_inv -= 2.625f / d1) * t_inv + 0.984375f;
            return 1.0f - res;
        }
        case Easing::InOutBounce: {
            // Half InBounce, Half OutBounce
            if (t < 0.5f) {
                f32 t_sub = 1.0f - 2.0f * t;
                const f32 n1 = 7.5625f, d1 = 2.75f;
                f32 res;
                if (t_sub < 1.0f / d1) res = n1 * t_sub * t_sub;
                else if (t_sub < 2.0f / d1) res = n1 * (t_sub -= 1.5f / d1) * t_sub + 0.75f;
                else if (t_sub < 2.5f / d1) res = n1 * (t_sub -= 2.25f / d1) * t_sub + 0.9375f;
                else res = n1 * (t_sub -= 2.625f / d1) * t_sub + 0.984375f;
                return (1.0f - res) * 0.5f;
            } else {
                f32 t_sub = 2.0f * t - 1.0f;
                const f32 n1 = 7.5625f, d1 = 2.75f;
                f32 res;
                if (t_sub < 1.0f / d1) res = n1 * t_sub * t_sub;
                else if (t_sub < 2.0f / d1) res = n1 * (t_sub -= 1.5f / d1) * t_sub + 0.75f;
                else if (t_sub < 2.5f / d1) res = n1 * (t_sub -= 2.25f / d1) * t_sub + 0.9375f;
                else res = n1 * (t_sub -= 2.625f / d1) * t_sub + 0.984375f;
                return res * 0.5f + 0.5f;
            }
        }
        case Easing::Hold: return 0.0f;

        default: return t;
    }
}

} // namespace easing

using Ease = Easing;

} // namespace chronon3d
