#pragma once

// Effect stack: ordered list of post-processing effects applied to a layer.
// Order is preserved: blur then tint != tint then blur.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/effects/effect_instance.hpp>
#include <algorithm>
#include <variant>
#include <vector>

namespace chronon3d {

struct BlurParams          { f32   radius{0.0f}; };
struct TintParams          { Color color{0,0,0,0}; f32 amount{1.0f}; };
struct BrightnessParams    { f32   value{0.0f}; };
struct ContrastParams      { f32   value{1.0f}; };
struct DropShadowParams    { Vec2 offset{0,8}; Color color{0,0,0,0.35f}; f32 radius{12}; };
struct GlowParams {
    f32 radius{15.0f};
    f32 intensity{0.8f};
    Color color{1,1,1,1};
    f32 threshold{0.0f};
    f32 spread{1.0f};
    f32 softness{1.0f};
    f32 falloff{0.85f};
    f32 core_strength{0.70f};
    f32 aura_strength{0.35f};
    f32 bloom_strength{0.18f};
    bool additive{true};
};
struct BloomParams         { f32 threshold{0.80f}; f32 radius{24.0f}; f32 intensity{0.60f}; };

enum class WaveAxis {
    Horizontal,
    Vertical,
};

struct Fake3DWaveParams {
    f32 amplitude_px{18.0f};
    f32 frequency{2.2f};
    f32 speed{3.5f};
    f32 depth_px{35.0f};
    f32 phase{0.0f};
    i32 slices{24};
    WaveAxis axis{WaveAxis::Horizontal};
    f32 perspective{0.06f};
    f32 highlight{0.20f};
    f32 side_darkening{0.18f};
    bool shadow_enabled{true};
    Color shadow_color{1.0f, 0.05f, 0.05f, 0.75f};
    Vec2 shadow_offset{10.0f, 8.0f};
    f32 shadow_blur{0.0f};
    bool expand_bounds{true};
};

// Unify with the modular effects system
using EffectInstance = effects::EffectInstance;
using EffectStack = std::vector<EffectInstance>;

[[nodiscard]] inline f32 glow_effect_extent(const GlowParams& p) {
    const f32 radius = std::max(0.0f, p.radius) * std::max(0.0f, p.spread);
    return radius * 4.5f;
}

namespace GlowPresets {

[[nodiscard]] inline GlowParams neon_blue(f32 radius = 45.0f) {
    return GlowParams{
        .radius = radius,
        .intensity = 1.25f,
        .color = Color{0.15f, 0.65f, 1.0f, 1.0f},
        .threshold = 0.0f,
        .spread = 1.0f,
        .softness = 1.0f,
        .falloff = 0.82f,
        .core_strength = 0.85f,
        .aura_strength = 0.42f,
        .bloom_strength = 0.20f,
        .additive = true
    };
}

[[nodiscard]] inline GlowParams cinematic_gold(f32 radius = 55.0f) {
    return GlowParams{
        .radius = radius,
        .intensity = 1.10f,
        .color = Color{1.0f, 0.72f, 0.22f, 1.0f},
        .threshold = 0.0f,
        .spread = 1.0f,
        .softness = 1.0f,
        .falloff = 0.90f,
        .core_strength = 0.70f,
        .aura_strength = 0.35f,
        .bloom_strength = 0.16f,
        .additive = true
    };
}

[[nodiscard]] inline GlowParams soft_cyan(f32 radius = 38.0f) {
    return GlowParams{
        .radius = radius,
        .intensity = 0.90f,
        .color = Color{0.30f, 0.95f, 1.0f, 1.0f},
        .threshold = 0.0f,
        .spread = 1.0f,
        .softness = 1.0f,
        .falloff = 0.78f,
        .core_strength = 0.60f,
        .aura_strength = 0.38f,
        .bloom_strength = 0.22f,
        .additive = true
    };
}

[[nodiscard]] inline GlowParams soft_premium(f32 radius = 34.0f) {
    return GlowParams{
        .radius = radius,
        .intensity = 0.72f,
        .color = Color{0.46f, 0.82f, 1.0f, 1.0f},
        .threshold = 0.03f,
        .spread = 0.96f,
        .softness = 1.18f,
        .falloff = 1.08f,
        .core_strength = 0.50f,
        .aura_strength = 0.24f,
        .bloom_strength = 0.08f,
        .additive = false
    };
}

[[nodiscard]] inline GlowParams cinematic_gold_premium(f32 radius = 58.0f) {
    return GlowParams{
        .radius = radius,
        .intensity = 0.95f,
        .color = Color{1.0f, 0.80f, 0.30f, 1.0f},
        .threshold = 0.06f,
        .spread = 1.00f,
        .softness = 1.12f,
        .falloff = 1.05f,
        .core_strength = 0.56f,
        .aura_strength = 0.30f,
        .bloom_strength = 0.10f,
        .additive = false
    };
}

[[nodiscard]] inline GlowParams neon_sign(f32 radius = 44.0f) {
    return GlowParams{
        .radius = radius,
        .intensity = 1.12f,
        .color = Color{0.16f, 0.68f, 1.0f, 1.0f},
        .threshold = 0.0f,
        .spread = 1.05f,
        .softness = 0.88f,
        .falloff = 0.80f,
        .core_strength = 0.80f,
        .aura_strength = 0.32f,
        .bloom_strength = 0.12f,
        .additive = true
    };
}

[[nodiscard]] inline GlowParams editorial_highlight(f32 radius = 28.0f) {
    return GlowParams{
        .radius = radius,
        .intensity = 0.60f,
        .color = Color{0.95f, 0.98f, 1.0f, 1.0f},
        .threshold = 0.14f,
        .spread = 0.90f,
        .softness = 1.30f,
        .falloff = 1.12f,
        .core_strength = 0.38f,
        .aura_strength = 0.18f,
        .bloom_strength = 0.05f,
        .additive = false
    };
}

} // namespace GlowPresets

} // namespace chronon3d
