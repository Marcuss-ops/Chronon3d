// ═══════════════════════════════════════════════════════════════════════════
//  cinematic_text_camera.hpp — Phase-2.3 umbrella forwarder.
//
//  Phase-2.3 mechanical split.  This header is now an UMBRELLA: instead
//  of redeclaring the 5 cinematic composition factories inline, it
//  re-exports them via 5 per-composition .hpp forwarder files living at:
//
//      content/showcases/cinematic/deep_parallax_cascade.hpp
//      content/showcases/cinematic/whip_pan_hero_reveal.hpp
//      content/showcases/cinematic/orbit_handheld_glow.hpp
//      content/showcases/cinematic/rack_focus_title_swap.hpp
//      content/showcases/cinematic/abyss_freefall_stagger.hpp
//
//  Each split .hpp owns 1 factory declaration; the matching split
//  .cpp owns 1 factory implementation.  This umbrella keeps the public
//  surface stable: every consumer (e.g. the Phase-2.2 split cinematic
//  showcase test TUs at tests/showcase/cinematic/test_cinematic_*.cpp)
//  continues to `#include "content/showcases/cinematic/cinematic_text_
//  camera.hpp"` without modification.  The 5 composition factories are
//  still reachable in the `chronon3d::content::anims` namespace.
//
//  The previous monolithic source-of-truth file
//  content/showcases/cinematic/cinematic_text_camera.cpp (667 LOC, 5
//  inline factory impls + anonymous-namespace shared helpers) is
//  deleted by Phase-2.3 Commit B and replaced by the 5 split .cpp
//  files in content/CMakeLists.txt (one-line replace).  At no point
//  during the split does the build break: Commit A (this commit)
//  is build-inert because all 5 impls are still defined in
//  cinematic_text_camera.cpp; Commit B atomically swaps the .cpp
//  side so each function has exactly 1 defining TU.
//
//  Composition factories (canonical order, NEVER reorder):
//    1. DeepParallaxCascade — Catmull-Rom Z-push through floating text.
//    2. WhipPanHeroReveal    — violent camera snap + stagger typewriter.
//    3. OrbitHandheldGlow    — slow Catmull-Rom orbit + wiggle3D shake.
//    4. RackFocusTitleSwap   — Vertigo dolly-zoom + opposing blur tracks.
//    5. AbyssFreefallStagger — letters drop in Z while camera slowly
//                              rolls.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>

// ── Per-composition split forwarders (Phase-2.3) ──────────────────────
// All 5 cinematic text + camera compositions are re-exported via the
// 5 split .hpp files below.  Reachable under
// chronon3d::content::anims::* fully-qualified names.
#include "content/showcases/cinematic/deep_parallax_cascade.hpp"
#include "content/showcases/cinematic/whip_pan_hero_reveal.hpp"
#include "content/showcases/cinematic/orbit_handheld_glow.hpp"
#include "content/showcases/cinematic/rack_focus_title_swap.hpp"
#include "content/showcases/cinematic/abyss_freefall_stagger.hpp"
