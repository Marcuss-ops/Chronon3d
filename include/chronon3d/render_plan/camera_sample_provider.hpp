#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <chronon3d/animation/temporal/temporal_samples.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <functional>
#include <string>

namespace chronon3d::render_plan {

/// Runtime-only source of fully resolved camera values. `sample` receives an
/// absolute fractional presentation frame and returns the complete camera
/// state for that instant. The stable identity participates in render and
/// scene cache keys; the callback itself is never serialized.
/// Consumer-owned calendar used by offline Motion producers and compilation
/// of runtime camera providers. Uses exactly the canonical shutter scheduler;
/// integer presentation samples are included for no-shutter/random access.
[[nodiscard]] inline std::vector<double> resolved_sample_frames(
    const CanvasSpec& canvas, const std::optional<MotionBlurPlan>& blur) {
    if (canvas.duration.integral() <= 0 || canvas.duration.integral() > 1'000'000 ||
        canvas.width <= 0 || canvas.height <= 0 || canvas.fps.numerator <= 0 || canvas.fps.denominator <= 0)
        throw std::invalid_argument("resolved sample calendar requires a valid bounded canvas");
    if (blur && (!std::isfinite(blur->shutter_angle) || blur->shutter_angle <= 0.f ||
        blur->shutter_angle > 360.f || blur->samples < 2 || blur->samples > 64))
        throw std::invalid_argument("resolved sample calendar has invalid shutter settings");
    const auto per_frame = blur ? static_cast<std::uint64_t>(blur->samples) + 1 : 1;
    if (per_frame * static_cast<std::uint64_t>(canvas.duration.integral()) > 1'000'000)
        throw std::invalid_argument("resolved sample calendar exceeds 1000000 samples");
    std::vector<double> frames;
    for (std::int64_t frame = 0; frame < canvas.duration.integral(); ++frame) {
        frames.push_back(static_cast<double>(frame));
        if (blur) {
            temporal::TemporalSampleParams params;
            params.shutter_angle_deg = blur->shutter_angle;
            const auto schedule = temporal::make_temporal_sample_plan(params, blur->samples, Frame{frame},
                canvas.fps, canvas.width, canvas.height);
            if (!schedule.valid()) throw std::invalid_argument("resolved sample calendar has an invalid shutter schedule");
            for (const auto& sample : schedule.contexts) frames.push_back(sample.time.frame);
        }
    }
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}

struct RuntimeCameraSampleProvider {
    std::string fingerprint;
    std::function<CameraPlan(double)> sample;
};

} // namespace chronon3d::render_plan
