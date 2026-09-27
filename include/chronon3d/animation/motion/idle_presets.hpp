#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace chronon3d::motion_presets {
namespace detail {

inline std::int64_t period_frames(float seconds, FrameRate fps) {
    if (!std::isfinite(seconds) || seconds <= 0.0f || fps.num() <= 0 || fps.den() <= 0)
        throw std::invalid_argument("idle motion preset requires a positive period and frame rate");
    const double frames = static_cast<double>(seconds) * fps.num() / fps.den();
    const auto rounded = static_cast<std::int64_t>(std::llround(frames));
    if (rounded < 4 || rounded > 65535)
        throw std::invalid_argument("idle motion preset period must fit in 4..65535 frames");
    return rounded;
}

template <typename Sample>
void bake_periodic_track(AnimatedValue<Vec3>& track,
                         Vec3 base,
                         Frame start,
                         Frame duration,
                         std::int64_t period,
                         Sample&& sample) {
    if (start.integral() < 0 || duration.integral() < 1 || duration.integral() > 65535)
        throw std::invalid_argument("idle motion preset duration must be in [1, 65535] frames and start non-negative");
    track.set(base);
    for (std::int64_t cycle = 0; cycle < duration.integral(); cycle += period) {
        for (int quarter = 0; quarter < 4; ++quarter) {
            const auto relative = cycle + static_cast<std::int64_t>(std::llround(
                static_cast<double>(period) * static_cast<double>(quarter) / 4.0));
            if (relative >= duration.integral()) continue;
            const float phase = static_cast<float>(quarter) * 0.25f;
            track.add_keyframe(start + Frame{relative}, sample(base, phase));
        }
    }
    const auto phase_frame = duration.integral() % period;
    const float phase = static_cast<float>(phase_frame) / static_cast<float>(period);
    track.add_keyframe(start + duration, sample(base, phase));
}

inline float finite_amplitude(float value, const char* label) {
    if (!std::isfinite(value))
        throw std::invalid_argument(std::string("idle motion preset ") + label + " must be finite");
    return value;
}

} // namespace detail

/// Gentle cyclic scale variation, with a four-second period.
inline void breathe(Layer& layer, Frame start, Frame duration, FrameRate fps,
                    float amplitude = 0.015f) {
    if (amplitude < 0.0f || amplitude > 1.0f)
        throw std::invalid_argument("breathe amplitude must be in [0, 1]");
    const auto period = detail::period_frames(4.0f, fps);
    const auto base = layer.transform.scale;
    detail::bake_periodic_track(layer.anim_transform.scale, base, start, duration, period,
        [amplitude](Vec3 value, float phase) {
            const float scale = 1.0f + amplitude * std::sin(phase * 6.2831853071795864769f);
            return value * scale;
        });
}

/// Vertical sinusoidal drift, with a 2.5-second period.
inline void bob(Layer& layer, Frame start, Frame duration, FrameRate fps,
                float amplitude = 8.0f) {
    amplitude = detail::finite_amplitude(amplitude, "bob amplitude");
    const auto period = detail::period_frames(2.5f, fps);
    const auto base = layer.transform.position;
    detail::bake_periodic_track(layer.anim_transform.position, base, start, duration, period,
        [amplitude](Vec3 value, float phase) {
            value.y += amplitude * std::sin(phase * 6.2831853071795864769f);
            return value;
        });
}

/// Smooth scale pulse that returns to baseline at both ends of a two-second cycle.
inline void pulse(Layer& layer, Frame start, Frame duration, FrameRate fps,
                  float amplitude = 0.03f) {
    if (!std::isfinite(amplitude) || amplitude < 0.0f || amplitude > 1.0f)
        throw std::invalid_argument("pulse amplitude must be in [0, 1]");
    const auto period = detail::period_frames(2.0f, fps);
    const auto base = layer.transform.scale;
    detail::bake_periodic_track(layer.anim_transform.scale, base, start, duration, period,
        [amplitude](Vec3 value, float phase) {
            const float scale = 1.0f + amplitude *
                (0.5f - 0.5f * std::cos(phase * 6.2831853071795864769f));
            return value * scale;
        });
}

/// Slow XY drift plus a subtle z rotation, with a six-second period.
inline void floatDrift(Layer& layer, Frame start, Frame duration, FrameRate fps,
                       float amplitude = 8.0f, float rotation_amplitude_deg = 0.5f) {
    amplitude = detail::finite_amplitude(amplitude, "floatDrift amplitude");
    rotation_amplitude_deg = detail::finite_amplitude(
        rotation_amplitude_deg, "floatDrift rotation amplitude");
    const auto period = detail::period_frames(6.0f, fps);
    const auto base_position = layer.transform.position;
    const Vec3 base_rotation = glm::degrees(glm::eulerAngles(layer.transform.rotation));
    detail::bake_periodic_track(layer.anim_transform.position, base_position, start, duration, period,
        [amplitude](Vec3 value, float phase) {
            value.x += amplitude * std::sin(phase * 6.2831853071795864769f);
            value.y += amplitude * std::sin(phase * 6.2831853071795864769f + 1.5707963267948966192f);
            return value;
        });
    detail::bake_periodic_track(layer.anim_transform.rotation_euler, base_rotation,
                            start, duration, period,
        [rotation_amplitude_deg](Vec3 value, float phase) {
            value.z += rotation_amplitude_deg * std::sin(phase * 6.2831853071795864769f);
            return value;
        });
}

} // namespace chronon3d::motion_presets
