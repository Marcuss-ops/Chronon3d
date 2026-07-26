#pragma once

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/time.hpp>
#include <algorithm>

namespace chronon3d {

enum class ClampMode {
    Clamp
};

// Extrapolate — forward-ticket TICKET-EXTRAPOLATE-ENUM bundle
// Additive-only (Cat-3 safe). Original ClampMode retained for backward
// compatibility; new code should prefer InterpolateOptions below.
//   Clamp   — t outside [0,1] is clamped (current ClampMode behavior).
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
// match the legacy ClampMode-only behavior, so a default-constructed
// InterpolateOptions is bit-equivalent to the current ClampMode default.
struct InterpolateOptions {
    Extrapolate left{Extrapolate::Clamp};
    Extrapolate right{Extrapolate::Clamp};
    EasingCurve easing{Easing::Linear};
};

inline f32 map(f32 value, f32 in_min, f32 in_max, f32 out_min, f32 out_max, ClampMode clamp = ClampMode::Clamp) {
    if (in_max == in_min) return out_min;
    f32 t = (value - in_min) / (in_max - in_min);
    if (clamp == ClampMode::Clamp) {
        t = std::clamp(t, 0.0f, 1.0f);
    }
    return out_min + (out_max - out_min) * t;
}

inline f32 interpolate(
    f32 input,
    f32 input_start,
    f32 input_end,
    f32 output_start,
    f32 output_end,
    EasingCurve easing = EasingCurve{},
    ClampMode clamp = ClampMode::Clamp
) {
    if (input_end == input_start) {
        return output_start;
    }

    f32 t = (input - input_start) / (input_end - input_start);
    if (clamp == ClampMode::Clamp) {
        t = std::clamp(t, 0.0f, 1.0f);
    }
    
    t = easing.apply(t);

    return output_start + (output_end - output_start) * t;
}

inline f32 interpolate(
    Frame frame,
    Frame input_start,
    Frame input_end,
    f32 output_start,
    f32 output_end,
    EasingCurve easing = EasingCurve{},
    ClampMode clamp = ClampMode::Clamp
) {
    return interpolate(
        static_cast<f32>(frame),
        static_cast<f32>(input_start),
        static_cast<f32>(input_end),
        output_start,
        output_end,
        easing,
        clamp
    );
}

} // namespace chronon3d
