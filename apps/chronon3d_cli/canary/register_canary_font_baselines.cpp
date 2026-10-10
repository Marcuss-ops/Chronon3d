// ============================================================================
// apps/chronon3d_cli/canary/register_canary_font_baselines.cpp
//
// Family 4/6 of the canary pack: the five static font/glow baselines.
//
// These are intentionally boring, and that is the point: no camera motion, no
// per-glyph animation, no duplicated text node.  Chronon3D shapes and
// rasterizes one complete word, centred on a black screen, with only a
// restrained halo.  They are the visual reference selected before typewriter,
// Apple V3 choreography or 3D motion is introduced, so they must stay free of
// everything that comes after them.
// ============================================================================

#include "canary_common.hpp"
#include "register_canary.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <string>

namespace chronon3d {
namespace {

    using namespace canary_detail;

    [[nodiscard]] Composition make_black_glow_static(const char *name,
                                                     const char *font_path,
                                                     f32 font_size) {

        return composition(
            black_glow_spec(name), [font_path, font_size](const FrameContext &ctx) -> Scene {
                SceneBuilder scene(ctx);
                scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                    l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
                });

                scene.layer("CHRONON_TEXT", [&](LayerBuilder &l) {
                    l.text("text", canary_text(
                        "CHRONON", font_path, font_size, 700,
                        Color{1.0f, 1.0f, 1.0f, 1.0f}, Vec2{1700.0f, 360.0f}));
                    // The black-backdrop tuning lives in the preset, so
                    // the authoring site names the look instead of
                    // restating five numbers that must stay in sync
                    // across every black-glow clip.
                    l.effect(GlowPresets::canary_black());
                });

                scene.camera_descriptor(canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                return scene.build();
            });
    }

} // namespace

void register_canary_font_baseline_compositions(CompositionRegistry &registry) {
    for (const auto &font : kAppleV3Fonts) {
        const std::string id = std::string{"black_glow_"} + font.suffix;
        registry.add(make_composition_descriptor(
            id,
            [id, font_path = font.path] {
                return make_black_glow_static(id.c_str(), font_path, 190.0f);
            }));
    }
}

} // namespace chronon3d
