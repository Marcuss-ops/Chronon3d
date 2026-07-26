#pragma once

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/time.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

// Extrapolate — canonical out-of-range policy.
//   Clamp   — t outside [0,1] is clamped.
//   Extend  — t outside [0,1] is left raw, output continues linearly
//             beyond the declared output range. Useful for spring
//             overshoot / bounce / elastic / back presets whose eased_t
//             legitimately leaves [0,1].
//   Wrap    — t outside [0,1] is wrapped into [0,1) modulo 1, allowing
//             looping easing curves (analogous to modulo-clamped time).
enum class Extrapolate {
    Clamp,
    Extend,
    Wrap
};

// InterpolateOptions — bundles the extrapolation policies + easing curve
// for the new interpolate(..., InterpolateOptions) overload chain (added
// in commit `feat(animation): add extrapolation policies`). Defaults
// match the default interpolation behavior.
struct InterpolateOptions {
    Extrapolate left{Extrapolate::Clamp};
    Extrapolate right{Extrapolate::Clamp};
    EasingCurve easing{Easing::Linear};
};

// ── detail helpers — TICKET-EXTRAPOLATE-ENUM Fase 2 ───────────────────────
// Internal helpers in chronon3d::animation::detail. Not exported to the
// public SDK surface (detail namespace convention). Used by the canonical
// interpolate(..., InterpolateOptions) overload below.
namespace animation::detail {

/// Wrap a normalized t value into [0, 1] per the Extrapolate policy.
///   Clamp  → std::clamp(t, 0, 1)
///   Extend → t (no clamping)
///   Wrap   → std::fmod(t, 1); negative result normalized back to [0, 1)
[[nodiscard]] inline f32 wrap_unit(f32 t, Extrapolate mode) {
    switch (mode) {
        case Extrapolate::Clamp:
            return std::clamp(t, 0.0f, 1.0f);
        case Extrapolate::Extend:
            return t;
        case Extrapolate::Wrap: {
            const f32 wrapped = t - std::floor(t);
            return wrapped < 0.0f ? wrapped + 1.0f : wrapped;
        }
    }
    return t; // unreachable (suppresses -Wreturn-type)
}

/// Apply asymmetric extrapolation to a normalized t value.
///   t < 0 → wrap_unit(t, left)
///   t > 1 → wrap_unit(t, right)
///   else → t (no extrapolation needed)
[[nodiscard]] inline f32 apply_extrapolation(f32 t,
                                             Extrapolate left,
                                             Extrapolate right) {
    if (t < 0.0f) return wrap_unit(t, left);
    if (t > 1.0f) return wrap_unit(t, right);
    return t;
}

} // namespace animation::detail

inline f32 map(f32 value, f32 in_min, f32 in_max, f32 out_min, f32 out_max,
               bool clamp = true) {
    if (in_max == in_min) return out_min;
    f32 t = (value - in_min) / (in_max - in_min);
    if (clamp) {
        t = std::clamp(t, 0.0f, 1.0f);
    }
    return out_min + (out_max - out_min) * t;
}

// ── interpolate(f32, ..., InterpolateOptions) — TICKET-EXTRAPOLATE-ENUM Fase 2 ──
//
// Canonical interpolation pipeline with explicit left/right extrapolation
// policies. This is the canonical math entry point; ergonomic easing
// overloads below delegate to it via InterpolateOptions.
inline f32 interpolate(
    f32 input,
    f32 input_start,
    f32 input_end,
    f32 output_start,
    f32 output_end,
    const InterpolateOptions& opts
) {
    if (input_end == input_start) {
        return output_start;
    }

    const f32 raw_t = (input - input_start) / (input_end - input_start);
    const bool outside_left = raw_t < 0.0f;
    const bool outside_right = raw_t > 1.0f;
    const Extrapolate policy = outside_left ? opts.left
                               : outside_right ? opts.right
                               : Extrapolate::Clamp;
    const f32 t = animation::detail::apply_extrapolation(
        raw_t, opts.left, opts.right);

    // Extend is intentionally linear outside the authored range. Easing
    // curves are defined for the normalized interval and must not distort
    // spring/back/elastic overshoot when it is explicitly extended.
    if ((outside_left || outside_right) && policy == Extrapolate::Extend) {
        return output_start + (output_end - output_start) * t;
    }

    const f32 eased_t = opts.easing.apply(t);

    return output_start + (output_end - output_start) * eased_t;
}

/// Ergonomic canonical overload: linear/eased interpolation always uses the
/// default Clamp policies. Explicit Extend/Wrap callers use InterpolateOptions.
inline f32 interpolate(
    f32 input,
    f32 input_start,
    f32 input_end,
    f32 output_start,
    f32 output_end,
    EasingCurve easing = EasingCurve{}) {
    return interpolate(input, input_start, input_end, output_start, output_end,
                       InterpolateOptions{
                           Extrapolate::Clamp, Extrapolate::Clamp, easing});
}

inline f32 interpolate(
    Frame frame,
    Frame input_start,
    Frame input_end,
    f32 output_start,
    f32 output_end,
    const InterpolateOptions& options
) {
    return interpolate(
        static_cast<f32>(frame.integral()),
        static_cast<f32>(input_start.integral()),
        static_cast<f32>(input_end.integral()),
        output_start,
        output_end,
        options);
}

inline f32 interpolate(
    Frame frame,
    Frame input_start,
    Frame input_end,
    f32 output_start,
    f32 output_end,
    EasingCurve easing = EasingCurve{}) {
    return interpolate(
        frame, input_start, input_end, output_start, output_end,
        InterpolateOptions{
            Extrapolate::Clamp, Extrapolate::Clamp, easing});
}

} // namespace chronon3d
