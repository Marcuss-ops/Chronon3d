// content/effects/glow/glow_02_orb_galaxy.cpp
//
// Cleanup pass (formerly the "TEST 2 — Orb Galaxy" gateway).  The orb galaxy
// composition, the SaaS premium thumbnails (`PremiumThumbnailButterySmooth`,
// `PremiumThumbnailSaaSBlue`, `PremiumThumbnailSaaSBlueAuthored`), the trivial
// `GlowBasicWord`, and the entire glow V2 A/B test suite
// (`glow_v2_ab_tests.cpp`) have all been REMOVED from the product surface.
//
// What remains here:
//   * `register_effect_compositions(CompositionRegistry&)` — the per-domain
//     registration gateway called by `register_content_modules.cpp`.  It still
//     owns the **engine diagnostic** compositions (camera reference suite)
//     under `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`.  All these compositions are
//     defined in `content/effects/ref_2_5d/reference_2_5d_suite_*` (the
//     `chronon3d_diagnostics` OBJECT library when CHRONON3D_BUILD_DIAGNOSTICS=ON).
//     This gateway file is the ONLY TU in `chronon3d_content` that needs to
//     stay linked (the rest of the cleanup pass removed every other product
//     composition + the glow V2 A/B tests).
//
// The `glow_test_common.hpp` include was dropped: nothing in this file (after
// the gut) needs `kW/kH/deep_bg/bottom_label`; consumers that still want
// those helpers (`reference_2_5d_suite_helpers.hpp`) include the header
// themselves.

#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d::content::effects {

// ── Camera-test forward declarations (defined in ref_2_5d/) ──────────────
//
// All camera reference compositions are defined inside
// `content/effects/ref_2_5d/reference_2_5d_suite_*` and live in the
// `chronon3d_diagnostics` OBJECT library when `CHRONON3D_BUILD_DIAGNOSTICS=ON`.
// The forward declarations here + the `registry.add(...)` calls below are the
// gateway between `register_content_modules.cpp` (which calls this function)
// and those TUs.
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
Composition floating_cards_test();        // ref_2_5d_suite_cards.cpp
Composition orbit_camera_test();          // ref_2_5d_suite_cards.cpp
Composition depth_fog_test();             // ref_2_5d_suite_scenes.cpp
Composition z_stack_parallax_test();      // ref_2_5d_suite_cards.cpp
Composition shadow_glow_consistency_test();  // ref_2_5d_suite_cards.cpp
Composition extreme_perspective_test();   // ref_2_5d_suite_scenes.cpp
Composition y_rotation_text_test();       // ref_2_5d_suite_scenes.cpp
#endif

// ── Per-domain registration ──────────────────────────────────────────────────
//
// Cleanup pass: orb galaxy + SaaS thumbnail + glow V2 A/B registrations are
// gone.  This gateway now only publishes engine diagnostic compositions
// (camera reference suite) so that `register_content_modules.cpp` keeps
// resolving them when `CHRONON3D_BUILD_DIAGNOSTICS` is on.
void register_effect_compositions(CompositionRegistry& registry) {
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    registry.add("FloatingCardsTest",          [](const CompositionProps&) { return floating_cards_test();          });
    registry.add("OrbitCameraTest",            [](const CompositionProps&) { return orbit_camera_test();            });
    registry.add("DepthFogTest",               [](const CompositionProps&) { return depth_fog_test();               });
    registry.add("ZStackParallaxTest",         [](const CompositionProps&) { return z_stack_parallax_test();        });
    registry.add("ShadowGlowConsistencyTest",  [](const CompositionProps&) { return shadow_glow_consistency_test(); });
    registry.add("ExtremePerspectiveTest",     [](const CompositionProps&) { return extreme_perspective_test();     });
    registry.add("YRotationTextTest",          [](const CompositionProps&) { return y_rotation_text_test();         });
#endif
}

} // namespace chronon3d::content::effects
