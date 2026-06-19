// content/anims/compositions/tilt_sweep_title_v2.cpp
//
// TiltSweepTitleV2 — Cinematic Push-In Title Reveal (2D path).
//
// Replaces the original enable_3d() + rotate_y() approach (whose 3D
// pipeline is currently bugged — text renders off-canvas).  This
// rewrite uses ONLY 2D animations via LayerBuilder, mirroring the
// cinematic_title_reveal pattern:
//
//   * opacity_anim()     — fast fade-in 0→0.45→1.0 over 16 frames
//   * blur_anim()        — focus-in 14→0→0→2 (sharp → blur exit)
//   * scale_anim()       — push-in with overshoot (0.96→1.04→1.00→0.98)
//   * position_anim()    — subtle right→centre→left horizontal drift
//   * l.glow()           — intensities halved (bloom doesn't bleed into text)
//   * title_shadow layer — offset black copy for soft lift-shadow
//
// 1920×1080 · 30 FPS · 150 frame (5 secondi).
//
// Render:
//   chronon3d_cli video TiltSweepTitleV2 --start 0 --end 150 --fps 30 -o output.mp4

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include "content/text/text_glow_helpers.hpp"

namespace chronon3d::content::anims {

using text::glow::AeGlowOptions;
using text::glow::apply_ae_glow;

Composition tilt_sweep_title_v2() {
    // ─────────────────────────────────────────────────────────────────────
    // TiltSweepTitleV2 — REWRITTEN to use the v1 (2D) path.
    //
    // The original used enable_3d() + rotate_y() for "true 3D rotation",
    // but the 3D pipeline is bugged in this codebase (text rendered off-canvas).
    // This rewrite simulates the same tilt visually using ONLY 2D
    // animations — mirroring the cinematic_title_reveal patterns:
    //
    //   * l.pin_to(Anchor::Center) — anchor at canvas centre
    //   * l.scale_anim()           — animates scale.x = cos(θ) to mimic
    //                                the perspective foreshortening of a
    //                                Y-axis rotation.  scale.y stays at 1.0
    //                                so the text height is preserved (no squash).
    //   * l.position_anim()        — subtle right→centre→left drift for
    //                                cinematic feel.
    //   * l.opacity(1.0f)          — fully visible always (the original
    //                                "fade-in/out" animation was eating frames).
    //   * l.glow(...)              — reduced intensities so the bloom does
    //                                NOT blur the foreground text (the
    //                                user's #1 complaint: text+glow merged).
    //   * l.text(...)              — pure white, sharp text on top.
    //
    // Original rotation timeline (-40°→-18°→-8°) maps to:
    //   frame   0: cos(-40°) = 0.766  (heavy foreshortening)
    //   frame 100: cos(-18°) = 0.951  (mild foreshortening)
    //   frame 150: cos( -8°) = 0.990  (nearly frontal)
    // ─────────────────────────────────────────────────────────────────────
    return composition({
        .name     = "TiltSweepTitleV2",
        .width    = 1920,
        .height   = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Background: clean near-black full-canvas rect (no vignette, no fancy).
        s.layer("background", [](LayerBuilder& l) {
            l.rect("black_background", {
                .size  = {1920.0f, 1080.0f},
                .color = Color{0.012f, 0.012f, 0.016f, 1.0f},
            });
        });

        // ── 0.5. Shadow text layer (DRAWN BEFORE FG title) ─────────────
        // Pattern: duplicate the FG text at offset (+3, +3) with a dark
        // color and 0.7 alpha.  When the FG white text draws on top, the
        // dark copy peeks out at +3,+3 as a soft "lift" shadow.
        // We replicate the same animation block as the FG title so the
        // shadow tracks the tilt / push-in / drift in lock-step.
        s.layer("title_shadow", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);

            l.opacity_anim()
                .key(Frame{0},   0.0f,  EasingCurve{Easing::OutCubic})
                .key(Frame{16},  0.7f,  EasingCurve{Easing::OutCubic})
                .key(Frame{150}, 0.7f,  EasingCurve{Easing::Linear});

            l.scale_anim()
                .key(Frame{0},
                     Vec3{0.96f, 0.96f, 1.0f},
                     EasingCurve{Easing::OutCubic})
                .key(Frame{18},
                     Vec3{1.04f, 1.04f, 1.0f},
                     EasingCurve{Easing::OutCubic})
                .key(Frame{75},
                     Vec3{1.00f, 1.00f, 1.0f},
                     EasingCurve{Easing::InOutSine})
                .key(Frame{150},
                     Vec3{0.98f, 0.98f, 1.0f},
                     EasingCurve{Easing::InSine});

            // Position offset of (+3, +3) px creates the "shadow" effect
            // when the FG white text at (0, 0, 0) draws on top.
            l.position_anim()
                .key(Frame{0},
                     Vec3{19.0f, 3.0f, 0.0f},
                     EasingCurve{Easing::OutCubic})
                .key(Frame{75},
                     Vec3{3.0f, 3.0f, 0.0f},
                     EasingCurve{Easing::InOutSine})
                .key(Frame{150},
                     Vec3{-12.0f, 3.0f, 0.0f},
                     EasingCurve{Easing::InSine});

            l.blur_anim()
                .key(Frame{0},   14.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18},  4.0f,  EasingCurve{Easing::OutCubic})
                .key(Frame{150}, 4.0f,  EasingCurve{Easing::Linear});

            l.text("artist_name_shadow", TextSpec{
                .content = {.value = "LIL DIRK"},
                .font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Poppins", .font_weight = 700, .font_size = 132.0f},
                .layout = {.box = {1280.0f, 180.0f}, .anchor = TextAnchor::Center, .centering_mode = TextCenteringMode::PixelInk, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .wrap = TextWrap::None, .tracking = 1.15f},
                .appearance = {.color = Color{0.0f, 0.0f, 0.0f, 1.0f}},
                .position = {0.0f, 0.0f, 0.0f}
            });
        });

        s.layer("title", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);

            // ────────────────────────────────────────────────────────────
            // CINEMATIC PUSH-IN REVEAL  (replaces the bad monotonic scale.x
            // widen pattern that the user flagged as "si allarga per lo
            // schermo").  Mirrors cinematic_title_reveal exactly:
            //   1. opacity fade-in fast  (0 → 1 over 16 frames)
            //   2. blur focus-in          (14 → 0 over 18 frames)
            //   3. push-in with overshoot (0.96 → 1.04 → 1.00)
            //      — uniform scale, no squash
            //   4. subtle drift            (16 px right → centre → −15)
            // ────────────────────────────────────────────────────────────

            l.opacity_anim()
                .key(Frame{0},   0.0f,  EasingCurve{Easing::OutCubic})
                .key(Frame{7},   0.45f, EasingCurve{Easing::OutCubic})
                .key(Frame{16},  1.0f,  EasingCurve{Easing::OutCubic})
                .key(Frame{138}, 1.0f,  EasingCurve{Easing::Linear})
                .key(Frame{150}, 1.0f,  EasingCurve{Easing::InCubic});

            l.blur_anim()
                .key(Frame{0},   14.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18},  0.0f,  EasingCurve{Easing::OutCubic})
                .key(Frame{138}, 0.0f,  EasingCurve{Easing::Linear})
                .key(Frame{150}, 2.0f,  EasingCurve{Easing::InCubic});

            // Push-in with subtle overshoot — NEVER monotonic widening.
            // scale.y matches scale.x (no squash / no stretch).
            l.scale_anim()
                .key(Frame{0},
                     Vec3{0.96f, 0.96f, 1.0f},
                     EasingCurve{Easing::OutCubic})
                .key(Frame{18},
                     Vec3{1.04f, 1.04f, 1.0f},
                     EasingCurve{Easing::OutCubic})
                .key(Frame{75},
                     Vec3{1.00f, 1.00f, 1.0f},
                     EasingCurve{Easing::InOutSine})
                .key(Frame{150},
                     Vec3{0.98f, 0.98f, 1.0f},
                     EasingCurve{Easing::InSine});

            l.position_anim()
                .key(Frame{0},
                     Vec3{16.0f, 0.0f, 0.0f},
                     EasingCurve{Easing::OutCubic})
                .key(Frame{75},
                     Vec3{0.0f, 0.0f, 0.0f},
                     EasingCurve{Easing::InOutSine})
                .key(Frame{150},
                     Vec3{-15.0f, 0.0f, 0.0f},
                     EasingCurve{Easing::InSine});

            // Glow with intensities HALVED — preserves cinematic look
            // without bloom bleeding into the foreground text.
            apply_ae_glow(l, AeGlowOptions{
                .inner_radius    = 6.0f,
                .mid_radius      = 18.0f,
                .bloom_radius    = 30.0f,
                .inner_intensity = 0.32f,
                .mid_intensity   = 0.14f,
                .bloom_intensity = 0.05f,
                .micro_shadow    = false,
                .falloff         = 0.94f,
            });

            l.text("artist_name", TextSpec{
                .content = {.value = "LIL DIRK"},
                .font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Poppins", .font_weight = 700, .font_size = 132.0f},
                .layout = {.box = {1280.0f, 180.0f}, .anchor = TextAnchor::Center, .centering_mode = TextCenteringMode::PixelInk, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .wrap = TextWrap::None, .tracking = 1.15f},
                .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}},
                .position = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::anims
