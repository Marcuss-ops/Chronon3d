#include <chronon3d/render_plan/spring_bake.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chronon3d::render_plan {

std::vector<float> bake_spring_progress(const SpringConfig& config,
                                        TimeSeconds segment_seconds,
                                        std::uint32_t fps_num,
                                        std::uint32_t fps_den,
                                        std::uint32_t samples_per_frame) {
    validate_spring_config(config);
    if (config.mass > 10000.0f || config.stiffness > 100000.0f ||
        config.damping > 100000.0f || std::abs(config.initial_velocity) > 1000000.0f)
        throw std::invalid_argument("spring bake config is outside supported finite bounds");
    if (!std::isfinite(segment_seconds) || segment_seconds <= 0.0 ||
        fps_num == 0 || fps_den == 0 || samples_per_frame == 0 ||
        samples_per_frame > 16)
        throw std::invalid_argument("spring bake requires a positive finite segment, frame rate, and sample count");

    const double intervals = segment_seconds * static_cast<double>(fps_num) /
                             static_cast<double>(fps_den) * static_cast<double>(samples_per_frame);
    const double nearest_integer = std::round(intervals);
    const double sample_count_value = std::abs(intervals - nearest_integer) < 1e-7
        ? nearest_integer : std::ceil(intervals);
    constexpr std::size_t kMaxSpringBakeSamples = 1'000'000;
    if (!std::isfinite(sample_count_value) || sample_count_value < 1.0 ||
        sample_count_value > static_cast<double>(kMaxSpringBakeSamples))
        throw std::length_error("spring bake sample count exceeds the 1000000 sample limit");

    const auto sample_count = static_cast<std::size_t>(sample_count_value);
    std::vector<float> progress;
    progress.reserve(sample_count + 1);
    for (std::size_t index = 0; index <= sample_count; ++index) {
        const double time = segment_seconds * static_cast<double>(index) /
                            static_cast<double>(sample_count);
        const float value = sample_spring(time, 0.0f, 1.0f, config).value;
        if (!std::isfinite(value))
            throw std::runtime_error("spring bake produced a non-finite progress value");
        progress.push_back(std::clamp(value, -0.5f, 1.5f));
    }
    progress.back() = 1.0f;
    return progress;
}

} // namespace chronon3d::render_plan
