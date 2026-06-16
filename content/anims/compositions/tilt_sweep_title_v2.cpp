// content/anims/compositions/tilt_sweep_title_v2.cpp
//
// TiltSweepTitleV2 — rotazione 3D pura, nessuno scale/blur/fade.
// Camera prospettica statica (Z=-1000) + enable_3d() + rotate_y().
// Testo centrato, glow stabile.
//
// Rispetto all'originale TiltSweepTitle:
//   - Nessun drift orizzontale (testo sempre centrato)
//   - Glow: TextGlowSpec cinematico (stabile) — no flickering
//   - Rotazione 3D vera: camera prospettica + layer.enable_3d() + Timeline API
//   - Scala fissa 1.0, opacità fissa 1.0 — niente push-in o fade
//
// 1920×1080 · 30 FPS · 150 frame (5 secondi).
//
// Render:
//   chronon3d_cli video TiltSweepTitleV2 --start 0 --end 150 --fps 30 -o output/tilt_sweep_v2.mp4

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <chronon3d/text/text_glow_spec.hpp>

namespace chronon3d::content::anims {

namespace {

void animate_tilt_sweep_v2(LayerBuilder& layer) {
    // Solo rotazione 3D pura — niente scale, niente blur, niente fade.
    // Opacità fissa a 1.0, scala fissa a 1.0.
    layer.enable_3d()
         .position({0.0f, 0.0f, 0.0f})
         .scale({1.0f, 1.0f, 1.0f})
         .opacity(1.0f);

    using namespace chronon3d::motion;

    layer.rotate_y(motion::timeline(-40.0f)
        .to(Frame{100}, -18.0f, EasingCurve{Easing::InOutSine})
        .to(Frame{150}, -8.0f, EasingCurve{Easing::InOutSine}));
}

} // anonymous namespace

Composition tilt_sweep_title_v2() {
    return composition({
        .name     = "TiltSweepTitleV2",
        .width    = 1920,
        .height   = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);

        // ── Camera: static 3D perspective at Z=-1000, looking at origin ─
        scene.camera().enable(true)
             .position({0.0f, 0.0f, -1000.0f})
             .zoom(1000.0f)
             .look_at({0.0f, 0.0f, 0.0f});

        // ── Background: near-black ───────────────────────────────────
        scene.layer("background", [](LayerBuilder& layer) {
            layer.rect("black_background", {
                .size  = {1920.0f, 1080.0f},
                .color = Color{0.006f, 0.006f, 0.008f, 1.0f},
                .pos   = {960.0f, 540.0f, 0.0f}
            });
        });

        // ── Title layer: centered, animated scale/opacity/blur + glow
        scene.layer("title", [](LayerBuilder& layer) {
            animate_tilt_sweep_v2(layer);

            // Glow: TextGlowSpec cinematico (stabile, come l’originale).
            TextGlowSpec glow = TextGlowPresets::ae_cinematic_white();
            glow.inner_radius    = 6.0f;
            glow.mid_radius      = 18.0f;
            glow.bloom_radius    = 40.0f;
            glow.inner_intensity = 0.50f;
            glow.mid_intensity   = 0.22f;
            glow.bloom_intensity = 0.08f;
            glow.softness        = 1.05f;
            glow.falloff         = 0.92f;
            glow.outer_downscale = 0.25f;
            layer.glow(glow.to_glow_params());

            layer.text("artist_name", TextParams{
                .text       = "LIL DIRK",
                .size       = {900.0f, 180.0f},
                .pos        = {0.0f, 0.0f, 0.0f},
                .font_path  = "assets/fonts/Georgia_Bold.ttf",
                .font_family = "Georgia",
                .font_weight = 700,
                .font_size  = 108.0f,
                .color      = Color{0.91f, 0.91f, 0.90f, 1.0f},
                .anchor     = TextAnchor::Center,
                .align      = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking   = 1.0f,
                .wrap       = TextWrap::None,
            });
        });

        return scene.build();
    });
}

} // namespace chronon3d::content::anims
