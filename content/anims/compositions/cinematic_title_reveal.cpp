// content/anims/compositions/cinematic_title_reveal.cpp
//
// Cinematic Title Reveal con Slow Push-In e Horizontal Drift.
//
// Effetto ispirato ai titoli cinematografici: il testo appare da una scala
// ridotta (60%), esegue un push-in morbido fino al centro dell'animazione,
// poi drifta lentamente da destra a sinistra (~50px totali).  Bianco su nero
// con glow, fade-in rapido e fade-out finale, blur in entrata e uscita.
//
// 1920×1080 · 30 FPS · 150 frame (5 secondi).
//
// Render:
//   chronon3d_cli video TiltSweepTitle --start 0 --end 150 --fps 30 -o output/tilt_sweep.mp4

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include <chronon3d/effects/effect_params.hpp>

#include <string>

namespace chronon3d::content::anims {

Composition tilt_sweep_title() {
    return composition({
        .name     = "TiltSweepTitle",
        .width    = 1920,
        .height   = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // ── Background: near-black with very subtle vignette ──────────────
        s.layer("background", [](LayerBuilder& l) {
            l.rect("bg", {
                .size  = {1920.0f, 1080.0f},
                .color = Color{0.006f, 0.006f, 0.008f, 1.0f},
            });
            l.vignette(0.45f, 0.55f, 0.60f);
        });

        // ── Title layer: animated position, scale, opacity, blur ─────────
        s.layer("title", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);

            // ── Position animation (horizontal drift, Y fixed at center) ──
            // Subtle rightward drift → slow center pull → gentle left drift.
            l.position_anim()
                .key(Frame{0},
                     Vec3{16.0f, 0.0f, 0.0f},
                     EasingCurve{Easing::OutCubic})
                .key(Frame{18},
                     Vec3{30.0f, 0.0f, 0.0f},
                     EasingCurve{Easing::InOutSine})
                .key(Frame{75},
                     Vec3{1.0f, 0.0f, 0.0f},
                     EasingCurve{Easing::InOutSine})
                .key(Frame{138},
                     Vec3{-30.0f, 0.0f, 0.0f},
                     EasingCurve{Easing::InOutSine})
                .key(Frame{150},
                     Vec3{-35.0f, 0.0f, 0.0f},
                     EasingCurve{Easing::InSine});

            // ── Scale animation (push-in from 62% to 100%) ───────────────
            // Rapid entry → peak at center → imperceptible pull-back.
            l.scale_anim()
                .key(Frame{0},
                     Vec3{0.62f, 0.62f, 1.0f},
                     EasingCurve{Easing::OutCubic})
                .key(Frame{18},
                     Vec3{0.94f, 0.94f, 1.0f},
                     EasingCurve{Easing::OutCubic})
                .key(Frame{75},
                     Vec3{1.00f, 1.00f, 1.0f},
                     EasingCurve{Easing::InOutSine})
                .key(Frame{138},
                     Vec3{0.95f, 0.95f, 1.0f},
                     EasingCurve{Easing::InOutSine})
                .key(Frame{150},
                     Vec3{0.93f, 0.93f, 1.0f},
                     EasingCurve{Easing::InSine});

            // ── Opacity (fade-in fast → hold → fade-out) ──────────────────
            l.opacity_anim()
                .key(Frame{0},   0.0f,  EasingCurve{Easing::OutCubic})
                .key(Frame{7},   0.45f, EasingCurve{Easing::OutCubic})
                .key(Frame{16},  1.0f,  EasingCurve{Easing::OutCubic})
                .key(Frame{138}, 1.0f,  EasingCurve{Easing::Linear})
                .key(Frame{150}, 0.0f,  EasingCurve{Easing::InCubic});

            // ── Blur (focus-in fast → sharp → slight defocus on exit) ────
            l.blur_anim()
                .key(Frame{0},   14.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18},  0.0f,  EasingCurve{Easing::OutCubic})
                .key(Frame{138}, 0.0f,  EasingCurve{Easing::Linear})
                .key(Frame{150}, 5.0f,  EasingCurve{Easing::InCubic});

            // ── Glow: cinematic white bloom (3-layer: inner/mid/bloom) ───
            // Uses TextGlowPresets::ae_cinematic_white() as base.
            TextGlowSpec glow = TextGlowPresets::ae_cinematic_white();
            glow.inner_radius    = 6.0f;
            glow.mid_radius      = 20.0f;
            glow.bloom_radius    = 40.0f;
            glow.inner_intensity = 0.55f;
            glow.mid_intensity   = 0.28f;
            glow.bloom_intensity = 0.12f;
            glow.softness        = 1.05f;
            glow.falloff         = 0.92f;
            glow.outer_downscale = 0.25f;
            l.glow(glow.to_glow_params());

            // ── Text: Georgia Bold, serif, cinematic white ────────────────
            l.text("artist_name", TextParams{
                .text      = "LIL DIRK",
                .size      = {900.0f, 180.0f},
                .pos       = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Georgia_Bold.ttf",
                .font_family = "Georgia",
                .font_weight = 700,
                .font_size = 118.0f,
                .color     = Color{0.94f, 0.94f, 0.92f, 1.0f},
                .anchor    = TextAnchor::Center,
                .centering_mode = TextCenteringMode::PixelInk,
                .align     = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking  = 1.0f,
                .wrap      = TextWrap::None,
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::anims
