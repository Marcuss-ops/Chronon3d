#pragma once

#include <chronon3d/animation/easing.hpp>
#include <algorithm>

namespace chronon3d {

inline f32 interpolate(
    f32 input,
    f32 input_start,
    f32 input_end,
    f32 output_start,
    f32 output_end,
    Easing easing = Easing::Linear
) {
    if (input_end == input_start) {
        return output_start;
    }

    f32 t = (input - input_start) / (input_end - input_start);
    t = std::clamp(t, 0.0f, 1.0f);
    
    if (easing != Easing::Linear) {
        t = easing::apply(easing, t);
    }

    return output_start + (output_end - output_start) * t;
}

inline f32 interpolate(
    Frame frame,
    Frame input_start,
    Frame input_end,
    f32 output_start,
    f32 output_end,
    Easing easing = Easing::Linear
) {
    return interpolate(
        static_cast<f32>(frame),
        static_cast<f32>(input_start),
        static_cast<f32>(input_end),
        output_start,
        output_end,
        easing
    );
}

} // namespace chronon3d
