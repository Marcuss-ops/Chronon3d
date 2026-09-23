// ============================================================================
// apps/chronon3d_cli/canary/register_canary_final3d.cpp
//
// Family 3/4 of the canary pack: the Final3D production catalog.
//
// These are Chronon3D-owned compositions.  Template intent is represented here
// as content definitions and motion is evaluated by this runtime; no preview
// rasterizer or per-character fallback participates.  One scene body carries
// all nine IDs because the differences between them are catalog switches —
// content line, glow preset, camera distance — not different engine surfaces.
// ============================================================================

#include "canary_common.hpp"
#include "canary_text_animation.hpp"
#include "register_canary.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <string>

namespace chronon3d {
namespace {

    using namespace canary_detail;

    // ── Final3D catalog: nine production composition IDs ───────────────────────
    [[nodiscard]] Composition make_final3d_catalog(const char *name) {
        return composition(
            canary_spec(name), [name](const FrameContext &ctx) -> Scene {
                SceneBuilder scene(ctx);
                const f32 t = canary_progress(ctx);
                const i32 frame = ctx.frame().integral();
                scene.screen_layer("BACKGROUND", [](LayerBuilder &l) { l.fill(kCanaryVoid); });

                const std::string id{name};
                const bool product = id == "product_orbit" || id == "macbook_product";
                const bool title_card = id == "title_card_3d";
                const bool typewriter = id == "typewriter_3d_glow";
                const bool clean_red = id == "clean_red" || id == "youtube_canary";

                scene.null_layer("FINAL3D_ROOT", [&](LayerBuilder &l) {
                    l.enable_3d(true)
                        .rotate({0.0f, canary_lerp(-8.0f, 8.0f, canary_smoothstep(t)), 0.0f});
                });

                if (product) {
                    scene.layer("PRODUCT", [&](LayerBuilder &l) {
                        l.parent("FINAL3D_ROOT").enable_3d(true)
                            .position({0.0f, -40.0f, 0.0f})
                            .rotate({0.0f, canary_lerp(-24.0f, 24.0f, t), 0.0f})
                            .scale({canary_lerp(0.72f, 1.0f, canary_smoothstep(t)),
                                    canary_lerp(0.72f, 1.0f, canary_smoothstep(t)), 1.0f});
                        l.effect(GlowPresets::canary_black(28.0f));
                        l.text("product_title", canary_text(
                            id == "macbook_product" ? "MACBOOK PRODUCT" : "PRODUCT ORBIT",
                            kCanaryFontBold, 132.0f, 700,
                            Color{0.94f, 0.97f, 1.0f, 1.0f},
                            id == "macbook_product" ? Vec2{1500.0f, 300.0f}
                                                       : Vec2{1300.0f, 300.0f}));
                    });
                } else {
                    const f32 opacity = typewriter
                        ? canary_smoothstep(static_cast<f32>(frame) / 36.0f)
                        : canary_smoothstep(static_cast<f32>(frame) / 12.0f);
                    scene.layer("TITLE", [&](LayerBuilder &l) {
                        l.parent("FINAL3D_ROOT").enable_3d(true)
                            .position({0.0f, title_card ? 70.0f : 0.0f, 0.0f})
                            .opacity(opacity)
                            .scale({canary_lerp(0.82f, 1.0f, canary_smoothstep(t)),
                                    canary_lerp(0.82f, 1.0f, canary_smoothstep(t)), 1.0f})
                            .rotate({canary_lerp(-8.0f, 8.0f, t),
                                     canary_lerp(-12.0f, 12.0f, t), 0.0f});
                        l.effect(GlowPresets::canary_black(34.0f));
                        l.text("title", typewriter
                            ? canary_element_text("TYPEWRITER 3D", ElementCanaryMode::Motion)
                            : canary_text(
                                clean_red ? "CHRONON" : "CHRONON",
                                kCanaryFontBold, 180.0f, 700,
                                clean_red ? Color{1.0f, 0.86f, 0.86f, 1.0f}
                                          : Color{1.0f, 1.0f, 1.0f, 1.0f},
                                Vec2{1600.0f, 360.0f}));
                    });
                    if (title_card) {
                        scene.layer("SUBTITLE", [&](LayerBuilder &l) {
                            l.parent("FINAL3D_ROOT").enable_3d(true)
                                .position({0.0f, -160.0f, 0.0f})
                                .opacity(canary_smoothstep(static_cast<f32>(frame - 18) / 18.0f));
                            l.text("subtitle", canary_text("MOTION ENGINE", kCanaryFontRegular,
                                                               68.0f, 400,
                                                               Color{0.88f, 0.92f, 1.0f, 1.0f},
                                                               Vec2{1400.0f, 120.0f}));
                        });
                    }
                }

                const f32 yaw = id == "camera_push" ? 0.0f : canary_lerp(-14.0f, 14.0f, t);
                const f32 distance = id == "camera_push"
                    ? canary_lerp(1120.0f, 880.0f, canary_smoothstep(t))
                    : (id == "light_pulse" ? 1010.0f : 1000.0f);
                scene.camera().set(canary_orbit_camera(yaw, canary_lerp(4.0f, -4.0f, t), distance));
                return scene.build();
            });
    }

} // namespace

void register_canary_final3d_compositions(CompositionRegistry &registry) {

    registry.add(make_composition_descriptor("logo_reveal",
                                             [] { return make_final3d_catalog("logo_reveal"); }));
    registry.add(make_composition_descriptor("product_orbit",
                                             [] { return make_final3d_catalog("product_orbit"); }));
    registry.add(make_composition_descriptor("title_card_3d",
                                             [] { return make_final3d_catalog("title_card_3d"); }));
    registry.add(make_composition_descriptor("camera_push",
                                             [] { return make_final3d_catalog("camera_push"); }));
    registry.add(make_composition_descriptor("light_pulse",
                                             [] { return make_final3d_catalog("light_pulse"); }));
    registry.add(make_composition_descriptor("macbook_product",
                                             [] { return make_final3d_catalog("macbook_product"); }));
    registry.add(make_composition_descriptor("typewriter_3d_glow",
                                             [] { return make_final3d_catalog("typewriter_3d_glow"); }));
    registry.add(make_composition_descriptor("clean_red",
                                             [] { return make_final3d_catalog("clean_red"); }));
    registry.add(make_composition_descriptor("youtube_canary",
                                             [] { return make_final3d_catalog("youtube_canary"); }));
}

} // namespace chronon3d
