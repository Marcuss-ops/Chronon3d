#pragma once

// Effect stack: ordered list of post-processing effects applied to a layer.
// Order is preserved: blur then tint != tint then blur.
//
// Parameter structs (BlurParams, GlowParams, etc.) and the EffectParams variant
// type are now in <chronon3d/effects/effect_params.hpp>.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <algorithm>
#include <variant>
#include <vector>

namespace chronon3d {

// Unify with the modular effects system
using EffectInstance = effects::EffectInstance;
using EffectStack = std::vector<EffectInstance>;

// ── effect_type_for specializations ─────────────────────────────────────────
// Each concrete param struct maps to its EffectType enum value.
// These are found by the template constructors in EffectInstance via
// dependent-name lookup at instantiation time.

namespace effects {

template <> struct effect_type_for<BlurParams>        { static constexpr EffectType value = EffectType::Blur; };
template <> struct effect_type_for<TintParams>        { static constexpr EffectType value = EffectType::Tint; };
template <> struct effect_type_for<BrightnessParams>  { static constexpr EffectType value = EffectType::Brightness; };
template <> struct effect_type_for<ContrastParams>    { static constexpr EffectType value = EffectType::Contrast; };
template <> struct effect_type_for<DropShadowParams>  { static constexpr EffectType value = EffectType::DropShadow; };
template <> struct effect_type_for<GlowParams>        { static constexpr EffectType value = EffectType::Glow; };
template <> struct effect_type_for<BloomParams>       { static constexpr EffectType value = EffectType::Bloom; };
template <> struct effect_type_for<Fake3DWaveParams>  { static constexpr EffectType value = EffectType::Fake3DWave; };
template <> struct effect_type_for<SaturationParams>  { static constexpr EffectType value = EffectType::Saturation; };
template <> struct effect_type_for<HueRotateParams>   { static constexpr EffectType value = EffectType::HueRotate; };
template <> struct effect_type_for<InvertParams>      { static constexpr EffectType value = EffectType::Invert; };
template <> struct effect_type_for<VignetteParams>    { static constexpr EffectType value = EffectType::Vignette; };

} // namespace effects

struct GlowStyle {
    Color color{1,1,1,1};
    f32 inner_radius{6.0f};
    f32 inner_opacity{0.75f};
    f32 mid_radius{22.0f};
    f32 mid_opacity{0.38f};
    f32 outer_radius{72.0f};
    f32 outer_opacity{0.16f};
    f32 outer_downscale{0.25f};
    BlendMode blend{BlendMode::Screen};
    bool preserve_source{true};

    operator GlowParams() const;
};

inline GlowStyle::operator GlowParams() const {
    GlowParams p;
    p.color = color;
    p.quality = GlowQuality::SkiaLike;
    p.preserve_source = preserve_source;
    p.blend = blend;
    p.additive = (blend == BlendMode::Add);
    p.layers = {
        {inner_radius, inner_opacity, 1.0f},
        {mid_radius, mid_opacity, 1.0f},
        {outer_radius, outer_opacity, outer_downscale}
    };
    p.radius = outer_radius;
    p.intensity = 1.0f;
    p.core_strength = inner_opacity;
    p.aura_strength = mid_opacity;
    p.bloom_strength = outer_opacity;
    p.outer_downscale = outer_downscale;
    return p;
}

[[nodiscard]] inline f32 glow_effect_extent(const GlowParams& p) {
    f32 base_radius = std::max(0.0f, p.radius);
    if (!p.layers.empty()) {
        f32 max_layer_r = 0.0f;
        for (const auto& l : p.layers) {
            max_layer_r = std::max(max_layer_r, l.radius);
        }
        base_radius = max_layer_r;
    }
    const f32 radius = base_radius * std::max(0.0f, p.spread);
    return radius + 4.0f;
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
        .outer_downscale = 0.25f,
        .preserve_source = true,
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
        .outer_downscale = 0.25f,
        .preserve_source = true,
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
        .outer_downscale = 0.25f,
        .preserve_source = true,
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
        .outer_downscale = 0.25f,
        .preserve_source = true,
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
        .outer_downscale = 0.25f,
        .preserve_source = true,
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
        .outer_downscale = 0.25f,
        .preserve_source = true,
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
        .outer_downscale = 0.25f,
        .preserve_source = true,
        .additive = false
    };
}

} // namespace GlowPresets

} // namespace chronon3d
