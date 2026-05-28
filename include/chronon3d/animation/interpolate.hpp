#pragma once

#include <chronon3d/animation/easing.hpp>
#include <chronon3d/core/types/time.hpp>
#include <algorithm>

namespace chronon3d {

enum class ClampMode {
    Clamp
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
