#pragma once

// Effect stack: ordered list of post-processing effects applied to a layer.
// Order is preserved: blur then tint != tint then blur.
//
// Parameter structs (BlurParams, GlowParams, etc.) and the EffectParams variant
// type are now in <chronon3d/effects/effect_params.hpp>.

#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/presets/glow_presets.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
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

// GlowStyle and GlowPresets are now in <chronon3d/effects/presets/glow_presets.hpp>

} // namespace chronon3d
