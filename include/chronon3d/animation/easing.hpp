#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/constants.hpp>
#include <cmath>
#include <optional>

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
        case Easing::InSine: return 1.0f - std::cos(t * math::pi * 0.5f);
        case Easing::OutSine: return std::sin(t * math::pi * 0.5f);
        case Easing::InOutSine: return -(std::cos(math::pi * t) - 1.0f) * 0.5f;
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
            const f32 c4 = (2.0f * math::pi) / 3.0f;
            return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f
                : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
        }
        case Easing::OutElastic: {
            const f32 c4 = (2.0f * math::pi) / 3.0f;
            return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f
                : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
        }
        case Easing::InOutElastic: {
            const f32 c5 = (2.0f * math::pi) / 4.5f;
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

struct CubicBezier {
    f32 x1{0.25f};
    f32 y1{0.10f};
    f32 x2{0.25f};
    f32 y2{1.00f};

    constexpr CubicBezier() = default;
    constexpr CubicBezier(f32 px1, f32 py1, f32 px2, f32 py2)
        : x1(px1), y1(py1), x2(px2), y2(py2) {}

    f32 sample(f32 t) const {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;

        // Coefficients of x(u)
        const f32 cx = 3.0f * x1;
        const f32 bx = 3.0f * (x2 - x1) - cx;
        const f32 ax = 1.0f - cx - bx;

        // Coefficients of y(u)
        const f32 cy = 3.0f * y1;
        const f32 by = 3.0f * (y2 - y1) - cy;
        const f32 ay = 1.0f - cy - by;

        auto sample_curve_x = [&](f32 u) -> f32 {
            return ((ax * u + bx) * u + cx) * u;
        };

        auto sample_curve_y = [&](f32 u) -> f32 {
            return ((ay * u + by) * u + cy) * u;
        };

        auto sample_curve_derivative_x = [&](f32 u) -> f32 {
            return (3.0f * ax * u + 2.0f * bx) * u + cx;
        };

        // Try Newton-Raphson method (typically 4-8 iterations are enough)
        f32 u = t;
        for (int i = 0; i < 8; ++i) {
            f32 x_val = sample_curve_x(u) - t;
            if (std::abs(x_val) < 1e-6f) {
                return sample_curve_y(u);
            }
            f32 dVal = sample_curve_derivative_x(u);
            if (std::abs(dVal) < 1e-6f) {
                break;
            }
            u -= x_val / dVal;
        }

        // Bisection fallback
        f32 t0 = 0.0f;
        f32 t1 = 1.0f;
        u = t;

        while (t0 < t1) {
            f32 x_val = sample_curve_x(u);
            if (std::abs(x_val - t) < 1e-6f) {
                return sample_curve_y(u);
            }
            if (t > x_val) {
                t0 = u;
            } else {
                t1 = u;
            }
            u = (t1 + t0) * 0.5f;
        }

        return sample_curve_y(u);
    }
};

struct EasingCurve {
    Easing preset{Easing::Linear};
    std::optional<CubicBezier> cubic;

    constexpr EasingCurve(Easing e = Easing::Linear) : preset(e), cubic(std::nullopt) {}
    constexpr EasingCurve(CubicBezier cb) : preset(Easing::Linear), cubic(cb) {}

    static EasingCurve preset_curve(Easing e) {
        return EasingCurve(e);
    }

    static EasingCurve cubic_bezier(f32 x1, f32 y1, f32 x2, f32 y2) {
        return EasingCurve(CubicBezier(x1, y1, x2, y2));
    }

    f32 apply(f32 t) const {
        if (cubic.has_value()) {
            return cubic->sample(t);
        }
        return easing::apply(preset, t);
    }

    constexpr bool operator==(const EasingCurve& other) const {
        if (cubic.has_value() != other.cubic.has_value()) return false;
        if (cubic.has_value()) {
            return cubic->x1 == other.cubic->x1 && cubic->y1 == other.cubic->y1 &&
                   cubic->x2 == other.cubic->x2 && cubic->y2 == other.cubic->y2;
        }
        return preset == other.preset;
    }

    constexpr bool operator==(Easing e) const {
        return !cubic.has_value() && preset == e;
    }

    constexpr bool operator!=(const EasingCurve& other) const {
        return !(*this == other);
    }

    constexpr bool operator!=(Easing e) const {
        return !(*this == e);
    }

    friend constexpr bool operator==(Easing e, const EasingCurve& curve) {
        return curve == e;
    }

    friend constexpr bool operator!=(Easing e, const EasingCurve& curve) {
        return !(curve == e);
    }
};

} // namespace chronon3d
