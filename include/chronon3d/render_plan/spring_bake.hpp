#pragma once

#include <chronon3d/animation/easing/spring.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <cstdint>
#include <vector>

namespace chronon3d::render_plan {

/// Sample a spring segment at a fixed number of samples per frame.
///
/// The returned values are normalized progress from 0 to 1. Intermediate
/// overshoot is retained and bounded to [-0.5, 1.5]; the last sample is the
/// exact target so the following segment starts without a discontinuity.
/// This is a pure bake helper; callers insert these values as linear keys.
[[nodiscard]] std::vector<float> bake_spring_progress(
    const SpringConfig& config,
    TimeSeconds segment_seconds,
    std::uint32_t fps_num,
    std::uint32_t fps_den,
    std::uint32_t samples_per_frame = 2);

} // namespace chronon3d::render_plan
