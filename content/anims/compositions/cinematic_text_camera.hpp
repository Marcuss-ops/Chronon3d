#pragma once

// ── 5 cinematic text + camera compositions ───────────────────────────
//
// New family of motion-typography compositions that combine:
//   * 2.5D camera primitives (CatmullRom path, Vertigo dolly-zoom,
//     whip-pan translation, orbit, roll, handheld wiggle)
//   * Existing text engines (TextAnimator split_units, BlockBuilder,
//     TextGlow, AE cinematic glow presets)
//
// Each composition registers as a top-level name so it appears in
// `chronon3d_cli list` and the telemetry dashboard.

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::anims {

// 1. DeepParallaxCascade — Catmull-Rom Z-push through floating text.
Composition deep_parallax_cascade();

// 2. WhipPanHeroReveal    — violent camera snap + stagger typewriter.
Composition whip_pan_hero_reveal();

// 3. OrbitHandheldGlow    — slow Catmull-Rom orbit + wiggle3D shake.
Composition orbit_handheld_glow();

// 4. RackFocusTitleSwap   — Vertigo dolly-zoom + opposing blur tracks.
Composition rack_focus_title_swap();

// 5. AbyssFreefallStagger — letters drop in Z while camera slowly rolls.
Composition abyss_freefall_stagger();

} // namespace chronon3d::content::anims
