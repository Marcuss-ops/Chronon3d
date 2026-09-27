#pragma once

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/animation/core/animated_value.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace chronon3d::render_plan::effect_presets {

inline void rgb_split(LayerPlan& layer, float separation = 4.0f) {
    if (!std::isfinite(separation) || separation < 0.0f || separation > 128.0f)
        throw std::invalid_argument("RGB split separation must be in [0, 128]");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    effect.chromatic_red_offset = separation;
    effect.chromatic_blue_offset = -separation;
    layer.effects.push_back(std::move(effect));
}

/// Add the engine's isotropic Gaussian blur as a lightweight lens-softening preset.
inline void lens_blur(LayerPlan& layer, float radius = 8.0f) {
    if (!std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
        throw std::invalid_argument("lens blur radius must be in [0, 256]");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    effect.radius = radius;
    layer.effects.push_back(std::move(effect));
}

/// Add bounded horizontal scanline bands using the existing square WaveWarp.
inline void scanlines(LayerPlan& layer, float intensity = 0.02f,
                      float spacing = 0.035f) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 0.25f ||
        !std::isfinite(spacing) || spacing < 0.002f || spacing > 1.0f)
        throw std::invalid_argument("scanline intensity/spacing is outside supported bounds");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::WaveWarp;
    effect.amount = intensity;
    effect.warp_wavelength = spacing;
    effect.warp_direction = {0.0f, 1.0f};
    effect.wave_type = 2;
    layer.effects.push_back(std::move(effect));
}

/// Compose horizontal square-wave distortion, RGB separation, and seeded RGB
/// grain. All three stages lower through the existing software effect stack.
inline void glitch(LayerPlan& layer, float intensity = 0.5f,
                   std::uint32_t seed = 0) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 1.0f)
        throw std::invalid_argument("glitch intensity must be in [0, 1]");
    LayerPlan::EffectPlan warp;
    warp.kind = LayerPlan::EffectKindPlan::WaveWarp;
    warp.amount = 0.025f * intensity;
    warp.warp_wavelength = 0.035f;
    warp.warp_speed = 0.0f;
    warp.warp_direction = {0.0f, 1.0f};
    warp.wave_type = 2; // WaveWarpParams::WaveType::Square
    layer.effects.push_back(std::move(warp));

    LayerPlan::EffectPlan split;
    split.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    split.chromatic_red_offset = 8.0f * intensity;
    split.chromatic_blue_offset = -8.0f * intensity;
    layer.effects.push_back(std::move(split));

    LayerPlan::EffectPlan grain;
    grain.kind = LayerPlan::EffectKindPlan::Noise;
    grain.amount = 0.12f * intensity;
    grain.seed = seed;
    grain.animated = true;
    grain.rgb_noise = true;
    grain.noise_size = 1.0f;
    layer.effects.push_back(std::move(grain));
}

/// Build a bounded moving aurora palette for a filled rect/rounded-rect/ellipse
/// layer. Its period is baked at quarter-period keyframes over the layer span.
inline GradientAnimationPlan aurora_gradient(
    Frame start, Frame duration, FrameRate fps, float period_seconds = 6.0f) {
    if (start.integral() < 0 || duration.integral() < 1 || duration.integral() > 65535 ||
        !std::isfinite(period_seconds) || period_seconds <= 0.0f ||
        fps.num() <= 0 || fps.den() <= 0)
        throw std::invalid_argument("aurora gradient requires bounded positive timing and frame rate");
    const double raw_period = static_cast<double>(period_seconds) * fps.num() / fps.den();
    const auto period = static_cast<std::int64_t>(std::llround(raw_period));
    if (period < 4 || period > 65535)
        throw std::invalid_argument("aurora gradient period must fit in 4..65535 frames");

    const std::array<Color, 4> palette{
        Color{0.05f, 0.78f, 0.66f, 1.0f},
        Color{0.12f, 0.34f, 0.92f, 1.0f},
        Color{0.58f, 0.16f, 0.82f, 1.0f},
        Color{0.92f, 0.25f, 0.53f, 1.0f}};
    const auto color_at = [&](float position, float phase) {
        const float coordinate = std::fmod(position * 4.0f + phase * 4.0f, 4.0f);
        const auto left = static_cast<std::size_t>(std::floor(coordinate)) % palette.size();
        const auto right = (left + 1) % palette.size();
        return chronon3d::lerp(palette[left], palette[right], coordinate - std::floor(coordinate));
    };
    const auto gradient_at = [&](float phase) {
        return graphics::GradientDefinition::linear(
            Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f},
            {{0.0f, color_at(0.0f, phase)},
             {0.5f, color_at(0.5f, phase)},
             {1.0f, color_at(1.0f, phase)}});
    };

    GradientAnimationPlan result;
    result.easing = "linear";
    for (std::int64_t cycle = 0; cycle < duration.integral(); cycle += period) {
        for (int quarter = 0; quarter < 4; ++quarter) {
            const auto relative = cycle + static_cast<std::int64_t>(std::llround(
                static_cast<double>(period) * static_cast<double>(quarter) / 4.0));
            if (relative >= duration.integral()) continue;
            result.keyframes.push_back({start + Frame{relative}, gradient_at(
                static_cast<float>(quarter) * 0.25f)});
        }
    }
    const float final_phase = static_cast<float>(duration.integral() % period) /
                              static_cast<float>(period);
    result.keyframes.push_back({start + duration, gradient_at(final_phase)});
    return result;
}

} // namespace chronon3d::render_plan::effect_presets
