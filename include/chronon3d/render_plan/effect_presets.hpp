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

/// Velocity-driven chromatic RGB separation. Offset direction and magnitude
/// scale strictly with layer velocity vector.
inline void velocity_rgb_split(LayerPlan& layer, Vec2 velocity, float separation = 4.0f) {
    if (!std::isfinite(separation) || separation < 0.0f || separation > 128.0f ||
        !std::isfinite(velocity.x) || !std::isfinite(velocity.y))
        throw std::invalid_argument("velocity RGB split parameters are invalid");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    effect.chromatic_red_offset = separation;
    effect.chromatic_blue_offset = -separation;
    effect.chromatic_offset_mode = 2; // Velocity mode
    effect.chromatic_velocity = {velocity.x, velocity.y};
    layer.effects.push_back(std::move(effect));
}

/// CRT display recipe: barrel lens curvature + scanline lattice + phosphor RGB separation + bloom glow + RF noise.
inline void crt(LayerPlan& layer, float curvature = 0.08f, float scanline_intensity = 0.035f,
                float rgb_separation = 2.5f, float bloom_amount = 0.35f,
                float rf_noise = 0.05f, std::uint32_t seed = 1984) {
    if (!std::isfinite(curvature) || curvature < -1.0f || curvature > 1.0f ||
        !std::isfinite(scanline_intensity) || scanline_intensity < 0.0f || scanline_intensity > 0.25f)
        throw std::invalid_argument("CRT recipe parameters are outside supported bounds");

    // 1. Lens Distortion (barrel curvature)
    LayerPlan::EffectPlan lens;
    lens.kind = LayerPlan::EffectKindPlan::LensDistortion;
    lens.lens_curvature = curvature;
    lens.lens_scale = 1.0f + std::abs(curvature) * 0.25f; // compensate zoom
    layer.effects.push_back(std::move(lens));

    // 2. Scanline bands
    scanlines(layer, scanline_intensity, 0.025f);

    // 3. Phosphor RGB split
    if (rgb_separation > 0.001f) {
        rgb_split(layer, rgb_separation);
    }

    // 4. Phosphor Bloom
    if (bloom_amount > 0.001f) {
        LayerPlan::EffectPlan bloom;
        bloom.kind = LayerPlan::EffectKindPlan::Bloom;
        bloom.bloom_threshold = 0.65f;
        bloom.bloom_intensity = bloom_amount;
        bloom.radius = 16.0f;
        layer.effects.push_back(std::move(bloom));
    }

    // 5. Analog RF noise
    if (rf_noise > 0.001f) {
        LayerPlan::EffectPlan grain;
        grain.kind = LayerPlan::EffectKindPlan::Noise;
        grain.amount = rf_noise;
        grain.seed = seed;
        grain.animated = true;
        layer.effects.push_back(std::move(grain));
    }
}

/// Paper/document fold: geometric crease deformation and lighting shading.
inline void paper_fold(LayerPlan& layer, float amount = 0.30f, float position = 0.5f,
                       bool horizontal = false, float radius = 0.20f) {
    if (!std::isfinite(amount) || amount < -1.0f || amount > 1.0f ||
        !std::isfinite(position) || position < 0.0f || position > 1.0f ||
        !std::isfinite(radius) || radius < 0.01f || radius > 1.0f)
        throw std::invalid_argument("paper fold parameters are outside supported bounds");

    // 1. Fold warp
    LayerPlan::EffectPlan fold;
    fold.kind = LayerPlan::EffectKindPlan::FoldWarp;
    fold.fold_axis = horizontal ? 1 : 0;
    fold.fold_position = position;
    fold.fold_amount = amount;
    fold.fold_radius = radius;
    fold.fold_falloff = 1.2f;
    fold.fold_perspective = 0.06f;
    layer.effects.push_back(std::move(fold));

    // 2. Crease shading (shadow and specular highlight)
    LayerPlan::EffectPlan crease;
    crease.kind = LayerPlan::EffectKindPlan::CreaseShading;
    crease.crease_axis = horizontal ? 1 : 0;
    crease.crease_position = position;
    crease.crease_width = 28.0f;
    crease.crease_dark_side = 0.35f * std::abs(amount);
    crease.crease_light_side = 0.20f * std::abs(amount);
    crease.crease_softness = 0.50f;
    layer.effects.push_back(std::move(crease));
}

/// Film halation: warm photochemical bleed in high-luminance areas.
inline void halation(LayerPlan& layer, float intensity = 1.0f, float threshold = 0.75f,
                     float radius = 22.0f,
                     Color tint = Color{1.0f, 0.32f, 0.12f, 1.0f}) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f ||
        !std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f ||
        !std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
        throw std::invalid_argument("halation parameters are outside supported bounds");

    LayerPlan::EffectPlan hal;
    hal.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    hal.response_threshold = threshold;
    hal.response_softness = 0.20f;
    hal.response_radius = radius;
    hal.response_tint = {tint.r, tint.g, tint.b, tint.a};
    hal.response_intensity = intensity;
    hal.response_blend = 0; // Screen
    layer.effects.push_back(std::move(hal));
}

/// Anamorphic lens flare streak: luminance trigger + horizontal streak convolution + additive tint.
inline void anamorphic_flare(LayerPlan& layer, float length = 56.0f, float intensity = 1.2f,
                             Color tint = Color{0.18f, 0.65f, 1.0f, 1.0f}) {
    if (!std::isfinite(length) || length < 0.0f || length > 512.0f ||
        !std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f)
        throw std::invalid_argument("anamorphic flare parameters are outside supported bounds");

    LayerPlan::EffectPlan flare;
    flare.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    flare.response_threshold = 0.80f;
    flare.response_softness = 0.15f;
    flare.response_radius = 8.0f;
    flare.response_tint = {tint.r, tint.g, tint.b, tint.a};
    flare.response_intensity = intensity;
    flare.response_blend = 1; // Add

    LayerPlan::EffectPlan streak;
    streak.kind = LayerPlan::EffectKindPlan::DirectionalBlur;
    streak.directional_angle = 0.0f; // Pure horizontal anamorphic streak
    streak.directional_length = length;
    streak.directional_samples = static_cast<int>(std::clamp(length * 0.5f, 8.0f, 64.0f));

    layer.effects.push_back(std::move(flare));
    layer.effects.push_back(std::move(streak));
}

/// Atmospheric convective heatwave: organic fractal turbulence with slow vertical drift.
inline void heatwave(LayerPlan& layer, float amount = 0.035f, float speed = 1.2f,
                     float size = 0.12f, float evolution = 0.0f) {
    if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f ||
        !std::isfinite(speed) || !std::isfinite(size) || size <= 0.0f)
        throw std::invalid_argument("heatwave parameters are outside supported bounds");

    LayerPlan::EffectPlan displace;
    displace.kind = LayerPlan::EffectKindPlan::TurbulentDisplace;
    displace.amount = amount;
    displace.warp_size = size;
    displace.warp_speed = speed;
    displace.warp_evolution = evolution;
    displace.warp_complexity = 3;
    layer.effects.push_back(std::move(displace));
}

/// Frosted glass backdrop composition.
inline void frosted_glass(LayerPlan& layer, const BackdropFilterPlan& filter = BackdropFilterPlan::glass_frosted()) {
    filter.validate();
    layer.backdrop_filter = filter;
}

// ── Light Leak V1 ──────────────────────────────────────────────────────────
//
// Ten canonical leak recipes composed from the existing luminance, glow,
// bloom, ray and dispersion primitives. Every recipe:
//   * is deterministic — identical inputs push identical EffectPlan fields;
//   * fails closed on out-of-range parameters;
//   * is a no-op at intensity 0 (a leak that adds nothing must not add cost);
//   * bounds total energy through the underlying effect validators, so an
//     SDR white plate cannot explode: screen/add blends saturate at the
//     compositor and the response intensity ceiling is enforced here.

namespace light_leak {

inline constexpr float kMaxIntensity = 4.0f;
inline constexpr Color kWarmTint{1.0f, 0.62f, 0.24f, 1.0f};
inline constexpr Color kAmberTint{1.0f, 0.80f, 0.45f, 1.0f};
inline constexpr Color kCoolTint{0.55f, 0.78f, 1.0f, 1.0f};

inline void require_intensity(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > kMaxIntensity)
        throw std::invalid_argument("light leak intensity must be in [0, 4]");
}

inline void require_radius(float radius) {
    if (!std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
        throw std::invalid_argument("light leak radius must be in [0, 256]");
}

/// Warm light bleeding in from one frame edge: luminance response (screen)
/// plus a matching warm glow.
inline void edge_warm(LayerPlan& layer, float intensity = 1.0f,
                      Color tint = kWarmTint, float radius = 28.0f) {
    require_intensity(intensity);
    require_radius(radius);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.55f;
    response.response_softness = 0.35f;
    response.response_radius = radius;
    response.response_tint = {tint.r, tint.g, tint.b, tint.a};
    response.response_intensity = std::min(intensity, 2.0f);
    response.response_blend = 0; // Screen
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = std::min(radius * 1.5f, 256.0f);
    glow.glow_intensity = std::min(0.30f * intensity, 1.2f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

/// Bloom growing from one corner: god rays anchored at the corner origin
/// plus a low-threshold bloom.
inline void corner_bloom(LayerPlan& layer, float intensity = 1.0f,
                         int corner = 0, Color tint = kAmberTint) {
    require_intensity(intensity);
    if (corner < 0 || corner > 3)
        throw std::invalid_argument("light leak corner must be 0..3 (TL, TR, BL, BR)");
    if (intensity <= 0.0f) return;
    const std::array<Vec2, 4> origins{Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f},
                                      Vec2{0.0f, 1.0f}, Vec2{1.0f, 1.0f}};
    LayerPlan::EffectPlan rays;
    rays.kind = LayerPlan::EffectKindPlan::LightRays;
    rays.light_rays_origin = {origins[corner].x, origins[corner].y};
    rays.light_rays_length = 420.0f;
    rays.light_rays_density = std::min(0.35f + 0.15f * intensity, 0.95f);
    rays.light_rays_color = {tint.r, tint.g, tint.b, tint.a};
    rays.light_rays_decay = 0.90f;
    layer.effects.push_back(std::move(rays));

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.55f;
    bloom.bloom_intensity = std::min(0.45f * intensity, 1.8f);
    bloom.radius = 32.0f;
    layer.effects.push_back(std::move(bloom));
}

/// Luminous band sweeping horizontally: warm response plus a long horizontal
/// streak convolution (the band motion itself is plan-level animation).
inline void horizontal_sweep(LayerPlan& layer, float intensity = 1.0f,
                             Color tint = kWarmTint) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.60f;
    response.response_softness = 0.30f;
    response.response_radius = 24.0f;
    response.response_tint = {tint.r, tint.g, tint.b, tint.a};
    response.response_intensity = std::min(intensity, 2.5f);
    response.response_blend = 0; // Screen
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan streak;
    streak.kind = LayerPlan::EffectKindPlan::DirectionalBlur;
    streak.directional_angle = 0.0f;
    streak.directional_length = 96.0f;
    streak.directional_samples = 32;
    layer.effects.push_back(std::move(streak));
}

/// Diagonal variant of the sweep at 45 degrees.
inline void diagonal_sweep(LayerPlan& layer, float intensity = 1.0f,
                           Color tint = kAmberTint) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    horizontal_sweep(layer, intensity, tint);
    LayerPlan::EffectPlan streak = layer.effects.back();
    layer.effects.pop_back();
    streak.directional_angle = 45.0f;
    layer.effects.push_back(std::move(streak));
}

/// Two opposed leaks: symmetric warm/cool response pair meeting mid-frame.
inline void dual_edge(LayerPlan& layer, float intensity = 1.0f) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan warm;
    warm.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    warm.response_threshold = 0.58f;
    warm.response_softness = 0.32f;
    warm.response_radius = 26.0f;
    warm.response_tint = {kWarmTint.r, kWarmTint.g, kWarmTint.b, kWarmTint.a};
    warm.response_intensity = std::min(intensity, 2.0f);
    warm.response_blend = 0;
    layer.effects.push_back(std::move(warm));

    LayerPlan::EffectPlan cool;
    cool.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    cool.response_threshold = 0.62f;
    cool.response_softness = 0.30f;
    cool.response_radius = 26.0f;
    cool.response_tint = {kCoolTint.r, kCoolTint.g, kCoolTint.b, kCoolTint.a};
    cool.response_intensity = std::min(0.8f * intensity, 2.0f);
    cool.response_blend = 0;
    layer.effects.push_back(std::move(cool));
}

/// Elegant prismatic edge: a small constant RGB dispersion (2 px per channel
/// by default) confined to the luminous zone by the response mask.
inline void prismatic(LayerPlan& layer, float intensity = 1.0f,
                      float dispersion_px = 2.0f) {
    require_intensity(intensity);
    if (!std::isfinite(dispersion_px) || dispersion_px < 0.0f || dispersion_px > 16.0f)
        throw std::invalid_argument("light leak prismatic dispersion must be in [0, 16] px");
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.65f;
    response.response_softness = 0.25f;
    response.response_radius = 20.0f;
    response.response_tint = {1.0f, 1.0f, 1.0f, 1.0f};
    response.response_intensity = std::min(intensity, 2.0f);
    response.response_blend = 0;
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan split;
    split.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    split.chromatic_red_offset = dispersion_px;
    split.chromatic_blue_offset = -dispersion_px;
    layer.effects.push_back(std::move(split));
}

/// Pulse look: one warm glow whose breathing is driven plan-level through the
/// light.glow intensity parameter track. The recipe pins the warm tint and a
/// safe radius; the animation owns the on-grow-off curve.
inline void pulse(LayerPlan& layer, float peak_intensity = 1.0f,
                  Color tint = kWarmTint, float radius = 48.0f) {
    require_intensity(peak_intensity);
    require_radius(radius);
    if (peak_intensity <= 0.0f) return;
    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = radius;
    glow.glow_intensity = std::min(0.35f * peak_intensity, 1.4f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

/// Film burn: intense warm response over a low threshold (additive) plus
/// bloom — bright enough to read as a transition matte between scenes.
inline void film_burn(LayerPlan& layer, float intensity = 2.0f,
                      float threshold = 0.35f, Color tint = kWarmTint) {
    require_intensity(intensity);
    if (!std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f)
        throw std::invalid_argument("light leak film burn threshold must be in [0, 1]");
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan burn;
    burn.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    burn.response_threshold = threshold;
    burn.response_softness = 0.22f;
    burn.response_radius = 30.0f;
    burn.response_tint = {tint.r, tint.g, tint.b, tint.a};
    burn.response_intensity = std::min(intensity, kMaxIntensity);
    burn.response_blend = 1; // Add — the burn must lift to white
    layer.effects.push_back(std::move(burn));

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = threshold + 0.10f;
    bloom.bloom_intensity = std::min(0.5f * intensity, 2.0f);
    bloom.radius = 24.0f;
    layer.effects.push_back(std::move(bloom));
}

/// Soft wide flare: very diffuse warm halation without any streak.
inline void soft_flare(LayerPlan& layer, float intensity = 1.0f,
                       Color tint = kAmberTint, float radius = 96.0f) {
    require_intensity(intensity);
    require_radius(radius);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.45f;
    response.response_softness = 0.45f;
    response.response_radius = radius;
    response.response_tint = {tint.r, tint.g, tint.b, tint.a};
    response.response_intensity = std::min(intensity, 2.0f);
    response.response_blend = 0; // Screen
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = std::min(radius, 256.0f);
    glow.glow_intensity = std::min(0.25f * intensity, 1.0f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

/// Focus glow behind a selected subject: a soft radial emphasis (large warm
/// glow plus a gentle bloom) that plans combine with the subject's own
/// 1.00 -> 1.02 scale emphasis.
inline void focus_glow(LayerPlan& layer, float intensity = 1.0f,
                       Color tint = kAmberTint) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 64.0f;
    glow.glow_intensity = std::min(0.30f * intensity, 1.2f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.70f;
    bloom.bloom_intensity = std::min(0.30f * intensity, 1.2f);
    bloom.radius = 20.0f;
    layer.effects.push_back(std::move(bloom));
}

/// Bokeh pass bonus: radial blur sampling plus a soft glow — background
/// lights melt into round bokeh discs.
inline void bokeh_pass(LayerPlan& layer, float intensity = 1.0f,
                       float amount = 0.35f, Color tint = kAmberTint) {
    require_intensity(intensity);
    if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f)
        throw std::invalid_argument("light leak bokeh amount must be in [0, 1]");
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::RadialBlur;
    blur.radial_center = {0.5f, 0.5f};
    blur.radial_amount = amount * 0.5f;
    blur.radial_render_samples = 16;
    layer.effects.push_back(std::move(blur));

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 40.0f;
    glow.glow_intensity = std::min(0.30f * intensity, 1.2f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

} // namespace light_leak

} // namespace chronon3d::render_plan::effect_presets
