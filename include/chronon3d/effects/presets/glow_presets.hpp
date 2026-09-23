#pragma once

// ── Glow presets — reusable GlowParams constructors ────────────────────────
//
/// @file    glow_presets.hpp
/// @brief   The single restrained GlowParams default.
///
/// Extracted from effect_stack.hpp to keep effect type mapping separate from
/// creative preset definitions.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/effects/effect_params.hpp>

namespace chronon3d {

// One restrained, neutral Gaussian halo used by the canonical black-backdrop
// canaries. Callers can override the radius or the three GlowParams directly.
namespace GlowPresets {

[[nodiscard]] inline GlowParams canary_black(f32 radius = 24.0f) {
    return GlowParams{.radius = radius, .intensity = 0.25f,
                      .color = Color{1.0f, 1.0f, 1.0f, 1.0f}};
}

} // namespace GlowPresets
} // namespace chronon3d
