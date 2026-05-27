#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
#include <glm/gtx/easing.hpp>
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

// Delegates to GLM's well-tested easing functions (glm/gtx/easing.hpp).
// HOLD has no GLM equivalent and returns 0.0.
inline f32 apply(Easing type, f32 t) {
    switch (type) {
        case Easing::Linear:       return t;
        case Easing::InQuad:       return glm::quadraticEaseIn(t);
        case Easing::OutQuad:      return glm::quadraticEaseOut(t);
        case Easing::InOutQuad:    return glm::quadraticEaseInOut(t);
        case Easing::InCubic:      return glm::cubicEaseIn(t);
        case Easing::OutCubic:     return glm::cubicEaseOut(t);
        case Easing::InOutCubic:   return glm::cubicEaseInOut(t);
        case Easing::InExpo:       return glm::exponentialEaseIn(t);
        case Easing::OutExpo:      return glm::exponentialEaseOut(t);
        case Easing::InOutExpo:    return glm::exponentialEaseInOut(t);
        case Easing::InSine:       return glm::sineEaseIn(t);
        case Easing::OutSine:      return glm::sineEaseOut(t);
        case Easing::InOutSine:    return glm::sineEaseInOut(t);
        case Easing::InBack:       return glm::backEaseIn(t);
        case Easing::OutBack:      return glm::backEaseOut(t);
        case Easing::InOutBack:    return glm::backEaseInOut(t);
        case Easing::InElastic:    return glm::elasticEaseIn(t);
        case Easing::OutElastic:   return glm::elasticEaseOut(t);
        case Easing::InOutElastic: return glm::elasticEaseInOut(t);
        case Easing::InBounce:     return glm::bounceEaseIn(t);
        case Easing::OutBounce:    return glm::bounceEaseOut(t);
        case Easing::InOutBounce:  return glm::bounceEaseInOut(t);
        case Easing::Hold:         return 0.0f;
        default:                   return t;
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
