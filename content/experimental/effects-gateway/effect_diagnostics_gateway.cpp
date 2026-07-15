// content/experimental/effects-gateway/effect_diagnostics_gateway.cpp
//
// Registration gateway for engine diagnostic compositions (camera reference
// suite).  All orb galaxy / SaaS thumbnail / glow V2 A/B test compositions
// were removed in a cleanup pass.
//
// The `register_effect_compositions()` function publishes camera reference
// compositions under `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`.  The actual
// composition factories live in `content/experimental/ref-2d5/`.

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

// ── Per-domain registration ──────────────────────────────────────────────
void register_effect_compositions(CompositionRegistry& registry) {
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    registry.add(CompositionDescriptor{.id = "FloatingCardsTest", .factory = [](const CompositionProps&) { return floating_cards_test();          }});
    registry.add(CompositionDescriptor{.id = "OrbitCameraTest", .factory = [](const CompositionProps&) { return orbit_camera_test();            }});
    registry.add(CompositionDescriptor{.id = "DepthFogTest", .factory = [](const CompositionProps&) { return depth_fog_test();               }});
    registry.add(CompositionDescriptor{.id = "ZStackParallaxTest", .factory = [](const CompositionProps&) { return z_stack_parallax_test();        }});
    registry.add(CompositionDescriptor{.id = "ShadowGlowConsistencyTest", .factory = [](const CompositionProps&) { return shadow_glow_consistency_test(); }});
    registry.add(CompositionDescriptor{.id = "ExtremePerspectiveTest", .factory = [](const CompositionProps&) { return extreme_perspective_test();     }});
    registry.add(CompositionDescriptor{.id = "YRotationTextTest", .factory = [](const CompositionProps&) { return y_rotation_text_test();         }});
#endif
}

} // namespace chronon3d::content::effects
