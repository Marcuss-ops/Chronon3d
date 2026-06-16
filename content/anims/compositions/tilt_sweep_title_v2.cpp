// content/anims/compositions/tilt_sweep_title_v2.cpp
//
// TiltSweepTitleV2 — titolo cinematografico stabile, puramente 2D.
// Testo centrato e fermo, solo push-in scala, fade e blur.
//
// Rispetto all'originale TiltSweepTitle:
//   - Nessun drift orizzontale (testo sempre centrato)
//   - Glow: TextGlowSpec cinematico (stabile) — no flickering
//   - Testo più piccolo (108pt), scala ridotta (max 0.94)
//   - Blur più rapido e meno aggressivo
//   - Fade-out morbido (opacità finale 0.12)
//   - Rotazione prospettica: enable_3d() + motion::Timeline API
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
    layer.enable_3d()
         .pin_to(Anchor::Center)
         .scale({0.50f, 0.50f, 1.0f})
         .opacity(0.0f)
         .blur(8.0f);

    // Rotazione prospettica: motion::Timeline API — easing sul segmento,
    // non ambiguamente sul keyframe.  Molto più leggibile.
    using namespace chronon3d::motion;

    layer.rotate_x(motion::timeline(-3.0f)
        .to(Frame{35}, -1.5f, EasingCurve{Easing::OutCubic})
        .to(Frame{75},  0.0f, EasingCurve{Easing::InOutSine})
        .hold_until(Frame{140})
        .to(Frame{150}, -0.5f, EasingCurve{Easing::InOutSine}));

    layer.rotate_y(motion::timeline(-25.0f)
        .to(Frame{35}, -14.0f, EasingCurve{Easing::OutCubic})
        .to(Frame{75},  -8.0f, EasingCurve{Easing::InOutSine})
        .hold_until(Frame{140})
        .to(Frame{150}, -10.0f, EasingCurve{Easing::InOutSine}));

    // ── Animated scale: motion::Timeline — uniform push-in. ──────────
    layer.scale_x(motion::timeline(0.50f)
        .to(Frame{18}, 0.86f, EasingCurve{Easing::OutCubic})
        .to(Frame{75}, 0.94f, EasingCurve{Easing::OutCubic})
        .to(Frame{138}, 0.90f, EasingCurve{Easing::InOutSine})
        .to(Frame{150}, 0.88f, EasingCurve{Easing::InOutSine}));

    layer.scale_y(motion::timeline(0.50f)
        .to(Frame{18}, 0.86f, EasingCurve{Easing::OutCubic})
        .to(Frame{75}, 0.94f, EasingCurve{Easing::OutCubic})
        .to(Frame{138}, 0.90f, EasingCurve{Easing::InOutSine})
        .to(Frame{150}, 0.88f, EasingCurve{Easing::InOutSine}));

    // ── Opacity: motion::Timeline — fade-in rapido, fade-out morbido.
    layer.opacity_timeline(motion::timeline(0.0f)
        .to(Frame{6}, 0.40f, EasingCurve{Easing::OutCubic})
        .to(Frame{14}, 1.0f, EasingCurve{Easing::OutCubic})
        .hold_until(Frame{141})
        .to(Frame{150}, 0.12f, EasingCurve{Easing::Linear}));

    // ── Blur: motion::Timeline — fuoco rapido, sfocatura finale.
    layer.blur_timeline(motion::timeline(8.0f)
        .to(Frame{10}, 0.0f, EasingCurve{Easing::OutCubic})
        .hold_until(Frame{140})
        .to(Frame{150}, 3.0f, EasingCurve{Easing::Linear}));
}

} // namespace

Composition tilt_sweep_title_v2() {
    return composition({
        .name     = "TiltSweepTitleV2",
        .width    = 1920,
        .height   = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);

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
