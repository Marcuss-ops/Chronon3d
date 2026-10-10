// ============================================================================
// apps/chronon3d_cli/canary/register_canary_final_cards.cpp
//
// Family 6/6 of the canary pack: the two final single-card variants of the
// font fleet — the card the font was selected on, and the smooth 3D float +
// orbit recut of it.
//
//   typewriter_final_poppins            the final typewriter card: intro over
//                                       54 frames (1.8 s), then text and
//                                       cursor hold still for the rest of the
//                                       five-second video.
//   black_glow_freeserif_smooth_orbit   the FreeSerif baseline with a smooth
//                                       3D float and a gentle dolly/orbit
//                                       camera, on the steady canary_black glow.
//
// Both keep the GPU-native text contract (translation/scale only, no
// unimplemented RenderSurface effect stack, no legacy 3D rotation fallback).
// ============================================================================

#include "canary_common.hpp"
#include "canary_text_animation.hpp"
#include "register_canary.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d {
namespace {

    using namespace canary_detail;

    [[nodiscard]] Composition make_typewriter_final(const char *name,
                                                     const char *font_path,
                                                     f32 font_size,
                                                     f32 cursor_edge) {
        return composition(
            black_glow_spec(name), [font_path, font_size, cursor_edge](
                const FrameContext &ctx) -> Scene {
                SceneBuilder scene(ctx);
                const i32 frame = ctx.frame().integral();
                const f32 reveal_t = std::clamp(
                    static_cast<f32>(frame) /
                        static_cast<f32>(kTypewriterRevealEnd.integral()),
                    0.0f, 1.0f);
                const f32 settle = canary_smoothstep(reveal_t);

                scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                    l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
                });

                // One shared 2D parent keeps the word and its cursor aligned
                // while remaining compatible with the native GPU text path.
                scene.null_layer("TYPEWRITER_ROOT", [&](LayerBuilder &l) {
                    l.position({0.0f, canary_lerp(22.0f, 0.0f, settle),
                                canary_lerp(-24.0f, 0.0f, settle)});
                });

                scene.layer("TYPEWRITER_TEXT", [&](LayerBuilder &l) {
                    l.parent("TYPEWRITER_ROOT")
                        .scale({canary_lerp(0.95f, 1.0f, settle),
                                canary_lerp(0.95f, 1.0f, settle), 1.0f});
                    l.text("text", canary_typewriter_text(
                        "CHRONON", font_path, font_size,
                        Color{1.0f, 1.0f, 1.0f, 1.0f}, Vec2{1700.0f, 360.0f}));
                });

                // The red underscore is a real Chronon text node, so its
                // glyph and glow still belong to Chronon3D.  It is not used
                // to rebuild or position the letters in the main word.
                // Its X position follows the reveal from the shaped run's
                // left edge to its right edge; the spacing itself remains
                // entirely owned by Chronon's layout.
                const f32 cursor_x = canary_lerp(-cursor_edge, cursor_edge, reveal_t) + 40.0f;
                f32 cursor_opacity = reveal_t < 1.0f
                    ? ((frame / 10) % 2 == 0 ? 1.0f : 0.58f)
                    : std::clamp(1.0f -
                                 static_cast<f32>(frame - kTypewriterRevealEnd.integral()) /
                                     12.0f,
                                 0.0f, 1.0f);
                scene.layer("TYPEWRITER_CURSOR", [&](LayerBuilder &l) {
                    l.parent("TYPEWRITER_ROOT")
                        .position({cursor_x, 6.0f, 0.0f})
                        .opacity(cursor_opacity);
                    l.text("underscore", canary_text(
                        "_", font_path, font_size * 0.82f, 700,
                        Color{1.0f, 0.035f, 0.035f, 1.0f}, Vec2{180.0f, 260.0f}));
                });

                scene.camera_descriptor(canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                return scene.build();
            });
    }

    [[nodiscard]] Composition make_black_glow_freeserif_smooth_orbit(
        const char *name, const char *font_path, f32 font_size) {
        return composition(
            black_glow_spec(name), [font_path, font_size](
                const FrameContext &ctx) -> Scene {
                SceneBuilder scene(ctx);
                const f32 t = canary_progress(ctx);
                const i32 frame = ctx.frame().integral();

                scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                    l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
                });

                // Smooth subtle entrance: opacity in over 20 frames, gentle spatial drift
                const f32 opacity = canary_smoothstep(std::clamp(static_cast<f32>(frame) / 20.0f, 0.0f, 1.0f));

                scene.null_layer("TEXT_ROOT", [&](LayerBuilder &l) {
                    l.enable_3d(true)
                        .rotate({canary_lerp(-3.0f, 3.0f, canary_smoothstep(t)),
                                 canary_lerp(-6.0f, 6.0f, canary_smoothstep(t)), 0.0f});
                });

                scene.layer("CHRONON_TEXT", [&](LayerBuilder &l) {
                    l.parent("TEXT_ROOT").enable_3d(true)
                        .position({0.0f, 0.0f, 0.0f})
                        .opacity(opacity)
                        .scale({canary_lerp(0.96f, 1.02f, canary_smoothstep(t)),
                                canary_lerp(0.96f, 1.02f, canary_smoothstep(t)), 1.0f});

                    l.text("text", canary_text(
                        "CHRONON", font_path, font_size, 700,
                        Color{1.0f, 1.0f, 1.0f, 1.0f}, Vec2{1700.0f, 360.0f}));

                    // Steady Font Fleet canary_black glow - static, clean, no pulsation
                    l.effect(GlowPresets::canary_black());
                });

                // Smooth 3D Dolly / Orbit camera: gentle yaw arc ±12°, subtle pitch, smooth push 1050 -> 950
                const f32 yaw = canary_lerp(-10.0f, 10.0f, canary_smoothstep(t));
                const f32 pitch = canary_lerp(4.0f, -3.0f, canary_smoothstep(t));
                const f32 distance = canary_lerp(1050.0f, 960.0f, canary_smoothstep(t));
                scene.camera_descriptor(canary_orbit_camera(yaw, pitch, distance));

                return scene.build();
            });
    }

} // namespace

void register_canary_final_card_compositions(CompositionRegistry &registry) {

    // Final typewriter card. Keep one composition while the native GPU
    // reveal path is validated; Poppins is the selected production font.
    registry.add(make_composition_descriptor(
        "typewriter_final_poppins",
        [] { return make_typewriter_final("typewriter_final_poppins",
                                          "assets/fonts/Poppins-Bold.ttf", 190.0f,
                                          500.0f); }));

    // Smooth 3D Floating + Orbit with font_fleet_v1_replay FreeSerif + CanaryBlack steady glow
    registry.add(make_composition_descriptor(
        "black_glow_freeserif_smooth_orbit",
        [] { return make_black_glow_freeserif_smooth_orbit(
            "black_glow_freeserif_smooth_orbit",
            "assets/fonts/FreeSerif.ttf", 190.0f); }));
}

} // namespace chronon3d
