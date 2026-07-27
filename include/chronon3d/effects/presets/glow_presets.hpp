#pragma once

// ── Glow presets — reusable GlowParams constructors ────────────────────────
//
/// @file    glow_presets.hpp
/// @brief   Pre-built GlowParams configurations (neon, cinematic, etc.).
///
/// Extracted from effect_stack.hpp to keep effect type mapping separate from
/// creative preset definitions.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/effects/effect_params.hpp>

namespace chronon3d {

// ── GlowPresets ─────────────────────────────────────────────────────────────
//
/// Factory functions returning pre-configured GlowParams for common glow styles.
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
        .blend = BlendMode::Add
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
        .blend = BlendMode::Add
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
        .blend = BlendMode::Add
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
        .blend = BlendMode::Screen
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
        .blend = BlendMode::Screen
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
        .blend = BlendMode::Add
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
        .blend = BlendMode::Screen
    };
}

} // namespace GlowPresets
} // namespace chronon3d
