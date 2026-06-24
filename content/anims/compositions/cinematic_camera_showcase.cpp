// ────────────────────────────────────────────────────────────────────────────
// content/anims/compositions/cinematic_camera_showcase.cpp
//
// CinematicCameraShowcase — 180-frame, 1920×1080, 30 fps (6-second) composition.
// See content/anims/compositions/cinematic_camera_showcase.hpp for DoD/spec.
//
// Strictly uses existing primitives (no motion logic added, no new registry
// entries). Pattern sources:
//   * Camera Bezier:        CameraTrajectoryBuilder::bezier_to chain
//                           (per tests/scene/camera/test_camera_trajectory.cpp)
//   * Per-frame sampling:   `CameraTrajectory::sample(CameraMotionContext)`
//                           yields {position, tangent, optional target, roll_deg}
//                           (per include/chronon3d/scene/camera/camera_v1/
//                           camera_trajectory.hpp)
//   * Text behaviors:       mirror of the four registered factory bodies in
//                           src/registry/text_preset_registry.cpp, but using
//                           LayerBuilder primitives directly (no registry call,
//                           no extension point addition — purely authoring-side)
// ────────────────────────────────────────────────────────────────────────────

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/backends/software/render_settings.hpp>  // MotionBlurMode + MotionBlurSettings
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>    // Camera2_5D + MotionBlurSettings

#include "content/anims/compositions/cinematic_camera_showcase.hpp"
#include "content/text/text_helpers.hpp"     // centered_text
#include "content/text/text_theme.hpp"       // FRESH_TEXT_WHITE / FRESH_TEXT_MUTED

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace chronon3d::content::anims {

namespace {

using chronon3d::content::text::centered_text;
using chronon3d::content::text::FRESH_TEXT_WHITE;
using chronon3d::content::text::FRESH_TEXT_MUTED;

// Camera world-space Bezier waypoints.
// These are CONTROL POINTS with REAL handle offsets — not straight lines.
// Local-offset semantics per CameraTrajectoryBuilder::bezier_to docs.
struct WaypointPalette {
    Vec3 p_far_entrance    = Vec3{-1500.0f, -120.0f,  +2200.0f};  // P0
    Vec3 p_curva_midpoint  = Vec3{ -180.0f,  +120.0f,  +800.0f};  // P1 (banking peak)
    Vec3 p_transition_hero = Vec3{ +280.0f,   +40.0f,  +200.0f};  // P1.5 (transition)
    Vec3 p_hero_close      = Vec3{ +800.0f,   +60.0f,     0.0f};  // P2 (final hold)
};

// Build the immutable 180-frame trajectory.  Three Bezier segments + a
// hold segment = 4 segments total.  Sum of durations == 180.
//
// Segment durations (frames):
//   seg0: 30   (Phase A + early B)  P0   → P1
//   seg1: 80   (rest of B + C)      P1   → P1.5
//   seg2: 40   (D)                  P1.5 → P2
//   seg3: 30   (E — final hold)     P2   static
std::shared_ptr<chronon3d::camera_v1::CameraTrajectory>
build_showcase_trajectory(const WaypointPalette& wp) {
    using chronon3d::camera_v1::CameraTrajectoryBuilder;

    return CameraTrajectoryBuilder()
        // ── seg0: dolly-in entrance (slow arcing curve, gentle rise) ─────
        .move_to(wp.p_far_entrance)
        .bezier_to(
            /*in  =*/ Vec3{ +600.0f,  +60.0f, -900.0f },   // tangent into P1 (offset from P1)
            /*out =*/ Vec3{ +400.0f,  +80.0f, -700.0f },   // tangent out  of P1
            /*to  =*/ wp.p_curva_midpoint
        ).duration_frames(30.0f)

        // ── seg1: banking curva (arcs to hero transition) ───────────────
        .bezier_to(
            Vec3{ +300.0f, -80.0f, -500.0f },               // into P1.5
            Vec3{ +200.0f, -40.0f, -300.0f },               // out  of P1.5
            wp.p_transition_hero
        ).duration_frames(80.0f)

        // ── seg2: hero approach (settling onto final) ───────────────────
        .bezier_to(
            Vec3{ +200.0f,   0.0f, -100.0f },               // into P2
            Vec3{ +100.0f,   0.0f,  +50.0f },               // out  of P2
            wp.p_hero_close
        ).duration_frames(40.0f)

        // ── seg3: hold final at P2 (35 frame ≥ 15 required by spec) ──────
        .hold_for(30.0f)

        // Cinematic-speed-uniform feel.
        .arc_length_parameterized(true)
        .build();
}

// Phase-conditional banking (rotation.z, i.e. "roll").
//   A:  0°    (entrance, untouched)
//   B:  ramp 0 → +15°
//   C:  ramp +15° → 0°
//   D:  0°    (sub-movement, keep-horizon retained)
//   E:  0°    (final hold; "keep_horizon" contract preserved.)
float banking_for_frame(Frame f) {
    const float pA = 0.0f;
    if (f < Frame{30})  return pA;
    if (f < Frame{70}) {
        // 0 → +15  linear across 40 frames (frames 30-70 = phase B)
        return 15.0f * (static_cast<float>(f) - 30.0f) / 40.0f;
    }
    if (f < Frame{110}) {
        // +15 → 0 across 40 frames (frames 70-110 = phase C ramp-down)
        return 15.0f * (1.0f - (static_cast<float>(f) - 70.0f) / 40.0f);
    }
    // Phase D (110-145) and Phase E (145-180): keep_horizon semantics, roll = 0.
    return 0.0f;
}

// Phase-conditional motion blur mode (Off ↔ TemporalAccumulation).
// Visible during curve peaks; off for entrance/hold so the final frame
// remains fully readable per spec.
MotionBlurMode motion_blur_mode_for_frame(Frame f) {
    const bool in_phase_B = (f >= Frame{30}  && f < Frame{70});
    const bool in_phase_D = (f >= Frame{110} && f < Frame{145});
    return (in_phase_B || in_phase_D)
        ? MotionBlurMode::TemporalAccumulation
        : MotionBlurMode::Off;
}

// Phase-conditional DOF focus distance (depth of field).
// Lerps from far (1800) to hero focus plane (0) over the hero-arrival
// phases, then static at hero (focus on the title).
float dof_focus_for_frame(Frame f) {
    const float t = std::min(1.0f, static_cast<float>(f) / 110.0f);
    return glm::mix(1800.0f, 0.0f, t);
}

// DOF aperture / max-blur settings.  Aperture starts loose (heavy bokeh
// for the long dolly-in) and tightens as the camera arrives on the hero.
struct DofTuning {
    float aperture;
    float max_blur;
};
DofTuning dof_tuning_for_frame(Frame f) {
    const float t = std::min(1.0f, static_cast<float>(f) / 110.0f);
    DofTuning d;
    d.aperture = glm::mix(0.045f, 0.020f, t);  // loose → tight
    d.max_blur = glm::mix(28.0f,  10.0f, t);   // strong bokeh → focused
    return d;
}

} // anonymous namespace

// ════════════════════════════════════════════════════════════════════════════
// CinematicCameraShowcase
// ════════════════════════════════════════════════════════════════════════════
Composition cinematic_camera_showcase() {
    return composition({
        .name     = "CinematicCameraShowcase",
        .width    = 1920,
        .height   = 1080,
        .duration = 180,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // ────────────────────────────────────────────────────────────────
        // 1. Camera trajectory — built once per composition call.
        // ────────────────────────────────────────────────────────────────
        const WaypointPalette wp;
        const auto traj = build_showcase_trajectory(wp);

        // ────────────────────────────────────────────────────────────────
        // 2. Depth plane: background gradient + far geometric set
        //    (Z = -1500, depth: deepest in scene).
        // ────────────────────────────────────────────────────────────────
        s.layer("bg_far", [](LayerBuilder& l) {
            l.rect("bg", {
                .size  = {1920.0f, 1080.0f},
                .color = {0.012f, 0.014f, 0.026f, 1.0f},
                .fill  = FillStyle::radial({960.0f, 540.0f}, 900.0f, {
                    {0.0f, {0.10f, 0.13f, 0.24f, 1.0f}},
                    {1.0f, {0.012f, 0.014f, 0.026f, 1.0f}},
                }),
            });
        });

        // ── Slow drifting geometric accents (back plane) ────────────────
        s.layer("bg_band_1", [](LayerBuilder& l) {
            l.position({0.0f, 360.0f, -1500.0f});
            l.opacity(0.55f);
            l.rounded_rect("band", {
                .size   = {1700.0f, 4.0f},
                .radius = 2.0f,
                .color  = {0.20f, 0.45f, 0.85f, 0.50f},
            });
            l.position_anim()
                .key(Frame{0},   Vec3{-200.0f, 360.0f, -1500.0f}, EasingCurve{Easing::Linear})
                .key(Frame{180}, Vec3{+200.0f, 360.0f, -1500.0f}, EasingCurve{Easing::InOutCubic});
        });
        s.layer("bg_band_2", [](LayerBuilder& l) {
            l.position({0.0f, -340.0f, -1500.0f});
            l.opacity(0.45f);
            l.rounded_rect("band", {
                .size   = {1500.0f, 3.0f},
                .radius = 1.5f,
                .color  = {0.85f, 0.30f, 0.65f, 0.45f},
            });
            l.position_anim()
                .key(Frame{0},   Vec3{+220.0f, -340.0f, -1500.0f}, EasingCurve{Easing::Linear})
                .key(Frame{180}, Vec3{-220.0f, -340.0f, -1500.0f}, EasingCurve{Easing::InOutCubic});
        });

        // ────────────────────────────────────────────────────────────────
        // 3. Hero title (DEPTH Z=0) — cinematic_title_reveal behavior.
        //    Prescription: scale_drop(0.85→1.0) + soft_pop + fade_in.
        // ────────────────────────────────────────────────────────────────
        s.layer("hero_title", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            // === mirror of cinematic_title_reveal factory body ===
            l.fade_in(Frame{18});
            l.scale_drop(0.85f, Frame{30});
            l.soft_pop(Frame{20});
            // CenteredTextSpec for the hero wordmark.
            TextSpec tp = centered_text({
                .text        = "CHRONON3D",
                .box         = {1600.0f, 320.0f},
                .font_size   = 168.0f,
                .tracking    = 8.0f,
                .color       = FRESH_TEXT_WHITE,
                .line_height = 1.10f,
            });
            l.text("hero_label", tp);
        });

        // ────────────────────────────────────────────────────────────────
        // 4. Closing subtitle (DEPTH Z=-400) — word_cascade behavior.
        //    Prescription: word_stagger (per-word delay) + fade_in.
        // ────────────────────────────────────────────────────────────────
        s.layer("subtitle", [](LayerBuilder& l) {
            l.position({0.0f, 200.0f, -400.0f});
            // === mirror of word_cascade factory body ===
            l.fade_in(Frame{18});
            l.word_stagger(Frame{4}, Frame{20});
            // Beheld-from-below tracking for hero complement.
            TextSpec tp = centered_text({
                .text        = "MOTION BUILT HERE",
                .box         = {1100.0f, 80.0f},
                .font_size   = 38.0f,
                .tracking    = 12.0f,
                .color       = {0.85f, 0.94f, 1.0f, 1.0f},
                .line_height = 1.10f,
            });
            l.text("sub_label", tp);
        });

        // ────────────────────────────────────────────────────────────────
        // 5. Continuous idle label (DEPTH Z=+500) — tracking_close behavior.
        //    Prescription: tracking_breathing (subtle letter-spacing pulse).
        //    Lifetime: entire composition; no entrance of its own.
        // ────────────────────────────────────────────────────────────────
        s.layer("version_label", [](LayerBuilder& l) {
            // absolute world coords — no pin_to() so the position is honoured
            // directly (anchor + position both set can re-interpret the
            // position as a relative offset, per code-review fix).
            l.position({850.0f, -380.0f, 500.0f});
            // === mirror of tracking_close factory body ===
            l.tracking_breathing(0.05f, Frame{30});
            // Stays visible throughout — alpha floor via opacity_anim.
            l.opacity_anim()
                .key(Frame{0},   0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{20},  1.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{180}, 1.0f, EasingCurve{Easing::Linear});
            TextSpec tp = centered_text({
                .text        = "v1.0",
                .box         = {240.0f, 64.0f},
                .font_size   = 36.0f,
                .tracking    = 6.0f,
                .color       = FRESH_TEXT_MUTED,
                .line_height = 1.10f,
            });
            l.text("version_label_text", tp);
        });

        // ────────────────────────────────────────────────────────────────
        // 6. Secondary per-glyph label (DEPTH Z=+700) — character_cascade.
        //    Prescription: glyph-level word_stagger + fade_in.
        //    Enters during Sub-movement phase D (frame 110) so it shows
        //    up just before the final hold — the second-movement kicker.
        // ────────────────────────────────────────────────────────────────
        s.layer("secondary_brand", [](LayerBuilder& l) {
            // absolute world coords — no pin_to() (see version_label comment).
            l.position({-760.0f, 360.0f, 700.0f});
            l.opacity_anim()
                .key(Frame{0},    0.0f, EasingCurve{Easing::Hold})
                .key(Frame{110},  0.0f, EasingCurve{Easing::Linear})
                .key(Frame{130},  1.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{180},  1.0f, EasingCurve{Easing::Linear});
            // === mirror of character_cascade factory body ===
            l.fade_in(Frame{15});
            l.word_stagger(Frame{2}, Frame{20});
            TextSpec tp = centered_text({
                .text        = "STUDIO CHRONON",
                .box         = {520.0f, 64.0f},
                .font_size   = 32.0f,
                .tracking    = 4.0f,
                .color       = {0.80f, 0.92f, 1.0f, 1.0f},
                .line_height = 1.10f,
            });
            l.text("brand_label", tp);
        });

        // ────────────────────────────────────────────────────────────────
        // 7. Camera: per-frame sample from Bezier trajectory → Camera2_5D.
        // ────────────────────────────────────────────────────────────────
        const chronon3d::camera_v1::CameraMotionContext mctx =
            chronon3d::camera_v1::CameraMotionContext::at(ctx.frame);
        const auto sample = traj->sample(mctx);

        Camera2_5D cam;
        cam.enabled = true;
        cam.position = sample.position;
        cam.zoom = 1100.0f;
        cam.fov_deg = 50.0f;

        // Orientation contract: ALWAYS look at the hero (Z=0). The
        // Agent-1 tangent-orient path is exercised by the
        // `camera_v1::CameraProgram::evaluate()` pipeline internally;
        // the showcase composes position + banking + POI directly because
        // it's authoring-side (not routed through default_camera_descriptor).
        cam.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
        cam.point_of_interest_enabled = true;

        // Banking (visible due to Agent-1 path roll_deg).
        cam.rotation = Vec3{ 0.0f, 0.0f, banking_for_frame(ctx.frame) };

        // Motion blur (active only in mid-curve phases B + D).
        cam.motion_blur.mode = motion_blur_mode_for_frame(ctx.frame);
        // pattern / filter use the struct's default-initialized values
        // (TemporalSamplePattern::Stratified, TemporalFilter::Box) — set in
        // MotionBlurSettings{} — so no override needed here.
        if (cam.motion_blur.mode == MotionBlurMode::TemporalAccumulation) {
            cam.motion_blur.samples           = 14;
            cam.motion_blur.shutter_angle_deg = 180.0f;
            cam.motion_blur.shutter_phase_deg = -90.0f;
            cam.motion_blur.jitter_seed       = 0xABCD1234u;
        } else {
            cam.motion_blur.samples           = 8;
            cam.motion_blur.shutter_angle_deg = 0.0f;
            cam.motion_blur.shutter_phase_deg = 0.0f;
        }

        // DOF (progressive focus on hero arrival; static afterwards).
        const DofTuning dof = dof_tuning_for_frame(ctx.frame);
        cam.dof.enabled            = true;
        cam.dof.use_physical_model = true;
        cam.dof.focus_distance     = dof_focus_for_frame(ctx.frame);
        cam.dof.aperture           = dof.aperture;
        cam.dof.max_blur           = dof.max_blur;

        s.camera().set(cam);

        return s.build();
    });
}

} // namespace chronon3d::content::anims
