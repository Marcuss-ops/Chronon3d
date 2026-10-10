// ============================================================================
// apps/chronon3d_cli/canary/register_canary_text.cpp
//
// Family 2/4 of the canary pack: the Chronon text-element contract canaries.
//
//   chronon_text_full_static      the plain full run, the reference picture
//   chronon_text_elements_static  B: every shaped element selected with an
//                                 identity visual property — must be
//                                 pixel-equivalent to the reference
//   chronon_text_elements_opacity C: the selector boundary animates and the
//                                 still-hidden elements are hidden
//   chronon_text_elements_motion  D: the same elements receive only motion
//                                 deltas on top of Chronon's own layout
//
// Each mode uses one complete Chronon text node; none combines a base run with
// glyph copies, and none rebuilds advances in the composition layer.
// ============================================================================

#include "canary_common.hpp"
#include "canary_text_animation.hpp"
#include "register_canary.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d {
namespace {

    using namespace canary_detail;

    [[nodiscard]] Composition make_text_elements_canary(
        const char *name, ElementCanaryMode mode) {
        return composition(
            canary_spec(name), [mode](const FrameContext &ctx) -> Scene {
                SceneBuilder scene(ctx);
                const bool moving = mode != ElementCanaryMode::Static;
                scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                    l.fill(kCanaryVoid);
                });

                scene.layer("CHRONON_TEXT_ELEMENTS", [&](LayerBuilder &l) {
                    l.enable_3d(true);
                    if (moving) {
                        l.rotate({0.0f, canary_lerp(-8.0f, 8.0f,
                                                    canary_progress(ctx)), 0.0f});
                    }
                    l.effect(GlowParams{.radius = 30.0f, .intensity = 0.25f, .color = Color::white()});
                    l.text("text", canary_element_text("CHRONON", mode));
                });

                scene.camera_descriptor(moving
                    ? canary_orbit_camera(
                        canary_lerp(-8.0f, 8.0f, canary_progress(ctx)), 0.0f, 1000.0f)
                    : canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                return scene.build();
            });
    }

    [[nodiscard]] Composition make_text_full_static_canary(const char *name) {
        return composition(
            canary_spec(name), [](const FrameContext &ctx) -> Scene {
                SceneBuilder scene(ctx);
                scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                    l.fill(kCanaryVoid);
                });

                scene.layer("CHRONON_TEXT_FULL_STATIC", [&](LayerBuilder &l) {
                    l.enable_3d(true);
                    l.effect(GlowParams{.radius = 30.0f, .intensity = 0.25f, .color = Color::white()});
                    l.text("text", canary_text(
                        "CHRONON", kCanaryFontBold, 180.0f, 700,
                        Color{1.0f, 0.86f, 0.86f, 1.0f}, Vec2{1600.0f, 360.0f}));
                });

                scene.camera_descriptor(canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                return scene.build();
            });
    }

} // namespace

void register_canary_text_compositions(CompositionRegistry &registry) {

    // Text contract canaries: B (static identity), C (opacity stagger),
    // D (opacity plus per-element motion delta).  Each uses one complete
    // Chronon text node; none combines a base run with glyph copies.
    registry.add(make_composition_descriptor(
        "chronon_text_full_static",
        [] { return make_text_full_static_canary("chronon_text_full_static"); }));
    registry.add(make_composition_descriptor(
        "chronon_text_elements_static",
        [] { return make_text_elements_canary("chronon_text_elements_static",
                                               ElementCanaryMode::Static); }));
    registry.add(make_composition_descriptor(
        "chronon_text_elements_opacity",
        [] { return make_text_elements_canary("chronon_text_elements_opacity",
                                               ElementCanaryMode::Opacity); }));
    registry.add(make_composition_descriptor(
        "chronon_text_elements_motion",
        [] { return make_text_elements_canary("chronon_text_elements_motion",
                                               ElementCanaryMode::Motion); }));
}

} // namespace chronon3d
