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

#include "cinematic_title_helpers.hpp"

namespace chronon3d::content::anims {

namespace {

// ── apply_tilt_sweep_glow ──────────────────────────────────────────────
// Glow: cinematic white bloom (3-layer: inner/mid/bloom).
// Uses TextGlowPresets::ae_cinematic_white() as base with custom tweaks.
void apply_tilt_sweep_glow(LayerBuilder& l) {
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
}

// ── apply_tilt_sweep_anim ──────────────────────────────────────────────
// Keyframe animation: position drift, scale push-in, opacity fade, blur.
void apply_tilt_sweep_anim(LayerBuilder& l) {
    l.pin_to(Anchor::Center);

    l.position_anim()
        .key(Frame{0},   Vec3{16.0f, 0.0f, 0.0f},  EasingCurve{Easing::OutCubic})
        .key(Frame{18},  Vec3{30.0f, 0.0f, 0.0f},  EasingCurve{Easing::InOutSine})
        .key(Frame{75},  Vec3{1.0f, 0.0f, 0.0f},   EasingCurve{Easing::InOutSine})
        .key(Frame{138}, Vec3{-30.0f, 0.0f, 0.0f},  EasingCurve{Easing::InOutSine})
        .key(Frame{150}, Vec3{-35.0f, 0.0f, 0.0f},  EasingCurve{Easing::InSine});

    l.scale_anim()
        .key(Frame{0},   Vec3{0.62f, 0.62f, 1.0f},  EasingCurve{Easing::OutCubic})
        .key(Frame{18},  Vec3{0.94f, 0.94f, 1.0f},  EasingCurve{Easing::OutCubic})
        .key(Frame{75},  Vec3{1.00f, 1.00f, 1.0f},  EasingCurve{Easing::InOutSine})
        .key(Frame{138}, Vec3{0.95f, 0.95f, 1.0f},  EasingCurve{Easing::InOutSine})
        .key(Frame{150}, Vec3{0.93f, 0.93f, 1.0f},  EasingCurve{Easing::InSine});

    l.opacity_anim()
        .key(Frame{0},   0.0f,  EasingCurve{Easing::OutCubic})
        .key(Frame{7},   0.45f, EasingCurve{Easing::OutCubic})
        .key(Frame{16},  1.0f,  EasingCurve{Easing::OutCubic})
        .key(Frame{138}, 1.0f,  EasingCurve{Easing::Linear})
        .key(Frame{150}, 0.0f,  EasingCurve{Easing::InCubic});

    l.blur_anim()
        .key(Frame{0},   14.0f, EasingCurve{Easing::OutCubic})
        .key(Frame{18},  0.0f,  EasingCurve{Easing::OutCubic})
        .key(Frame{138}, 0.0f,  EasingCurve{Easing::Linear})
        .key(Frame{150}, 5.0f,  EasingCurve{Easing::InCubic});
}

} // namespace

Composition tilt_sweep_title() {
    return composition({
        .name     = "TiltSweepTitle",
        .width    = 1920,
        .height   = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        add_cinematic_bg(s, /*centered=*/false, /*with_vignette=*/true);

        s.layer("title", [](LayerBuilder& l) {
            apply_tilt_sweep_anim(l);
            apply_tilt_sweep_glow(l);
            l.text("artist_name", make_artist_name_text(
                "LIL DIRK", Color{0.94f, 0.94f, 0.92f, 1.0f}));
        });

        return s.build();
    });
}

} // namespace chronon3d::content::anims
