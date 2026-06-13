#pragma once

// Effect stack: ordered list of post-processing effects applied to a layer.
// Order is preserved: blur then tint != tint then blur.
//
// Parameter structs (BlurParams, GlowParams, etc.) and the EffectParams variant
// type are now in <chronon3d/effects/effect_params.hpp>.
//
// effect_type_for<T> specialisations are generated automatically by the
// X-macro catalog in <chronon3d/effects/effect_catalog.hpp>.

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

// ── effect_type_for specialisations ─────────────────────────────────────────
// REMOVED: These are now generated automatically by the X-macro catalog in
// effect_catalog.hpp via #include "effect_catalog.def"
//
// See include/chronon3d/effects/effect_catalog.hpp for the auto-generated
// specialisations of effect_type_for<T> for all known param types.

// GlowStyle and GlowPresets are now in <chronon3d/effects/presets/glow_presets.hpp>

} // namespace chronon3d
