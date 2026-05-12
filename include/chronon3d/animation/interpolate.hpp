#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/core/time.hpp>
#include <algorithm>

namespace chronon3d {

inline f32 interpolate(
    f32 input,
    f32 input_start,
    f32 input_end,
    f32 output_start,
    f32 output_end
) {
    if (input_end == input_start) {
        return output_start;
    }

    f32 t = (input - input_start) / (input_end - input_start);
    t = std::clamp(t, 0.0f, 1.0f);

    return output_start + (output_end - output_start) * t;
}

inline f32 interpolate(
    Frame frame,
    Frame input_start,
    Frame input_end,
    f32 output_start,
    f32 output_end
) {
    return interpolate(
        static_cast<f32>(frame),
        static_cast<f32>(input_start),
        static_cast<f32>(input_end),
        output_start,
        output_end
    );
}

} // namespace chronon3d
