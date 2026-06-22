// content/anims/compositions/ae_camera_text_parity.cpp
//
// AECameraTextParity — 360-frame cinematic test composition that exercises
// Chronon3D's full camera stack for direct After Effects Classic 3D parity
// comparison.  Five identical "CHRONON3D" text labels parked at five
// different Z depths plus a ThreeNode rig with a `camera_target` null and
// a `focus_target` null.  Six 60-frame segments ramp through static,
// dolly-zoom, orbit, rack focus, whip pan w/ motion blur, and a stress
// segment that violates every parameter to expose edge cases.
//
// The layout is identical to a reference After Effects composition the
// user can build side-by-side:
//   • Composition 1920×1080, 30 fps, 360 frames
//   • Camera: Two-Node, position (0,0,-1200), target (0,0,0)
//   • Lens: 50 mm, 36×24 sensor, f/2.8, focus_distance 1200
//
// Render: see ae_camera_text_parity.hpp.

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <glm/glm.hpp>

#include "content/text/text_helpers.hpp"

#include <cmath>
#include <string>

namespace chronon3d::content::anims {

namespace {

// ── Shared scene fixtures (built once per frame) ────────────────────────────

void add_background(SceneBuilder& s) {
    s.layer("bg", [](LayerBuilder& l) {
        l.rect("bg", {
            .size  = {1920.0f, 1080.0f},
            .color = {0.08f, 0.09f, 0.12f, 1.0f},
        });
    });
}

void add_floor_grid(SceneBuilder& s) {
    // Floor at y=-150, XZ-aligned.  Grid lines spaced 100 units, low alpha.
    // Four corner markers at the grid's edges (sentinel for framing).
    s.layer("floor_grid", [](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, -150.0f, 0.0f});
        for (int i = -8; i <= 8; ++i) {
            const float k = static_cast<float>(i) * 100.0f;
            // Z-perp grid lines (parallel to X axis).
            l.rounded_rect("gx_" + std::to_string(i), {
                .size  = {1600.0f, 1.5f},
                .color = {1.0f, 1.0f, 1.0f, 0.12f},
                .pos   = {0.0f, 0.0f, k},
            });
            // X-perp grid lines (parallel to Z axis).
            l.rounded_rect("gz_" + std::to_string(i), {
                .size  = {1.5f, 1600.0f},
                .color = {1.0f, 1.0f, 1.0f, 0.12f},
                .pos   = {k, 0.0f, 0.0f},
            });
        }
        // Corner markers — 20×20 squares at the corners of the grid floor.
        const float corners[4][3] = {
            {-800.0f, 0.0f, -800.0f}, { 800.0f, 0.0f, -800.0f},
            {-800.0f, 0.0f,  800.0f}, { 800.0f, 0.0f,  800.0f},
        };
        for (int i = 0; i < 4; ++i) {
            l.rounded_rect("corner_" + std::to_string(i), {
                .size  = {20.0f, 20.0f},
                .color = {1.0f, 1.0f, 0.85f, 0.75f},
                .pos   = {corners[i][0], corners[i][1], corners[i][2]},
            });
        }
    });
}

void add_origin_axes(SceneBuilder& s) {
    // Red bar along X axis width 1600, blue bar along Y axis height 1600.
    s.layer("origin_axes", [](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 0.0f});
        l.rounded_rect("axis_x", {
            .size  = {1600.0f, 4.0f},
            .color = {1.0f, 0.15f, 0.15f, 0.85f},
            .pos   = {0.0f, 0.0f, 0.0f},
        });
        l.rounded_rect("axis_y", {
            .size  = {4.0f, 1600.0f},
            .color = {0.20f, 0.55f, 1.0f, 0.85f},
            .pos   = {0.0f, 0.0f, 0.0f},
        });
    });
}

void add_pedestal_cards(SceneBuilder& s) {
    // Three coloured cards parked BEHIND the deepest text label
    // (text_far lives at z=+700; cards sit at z=+1000/+1100/+1200 so they
    // are unambiguously further from the camera at every segment).
    // This exercises depth-buffer ordering for translucent layers.
    s.layer("pedestal_card_red", [](LayerBuilder& l) {
        l.enable_3d();
        l.position({-500.0f, -50.0f, 1000.0f});
        l.rounded_rect("card_red_rect", {
            .size  = {300.0f, 200.0f},
            .radius = 6.0f,
            .color = {0.9f, 0.2f, 0.2f, 0.50f},
        });
    });
    s.layer("pedestal_card_green", [](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 80.0f, 1100.0f});
        l.rounded_rect("card_green_rect", {
            .size  = {300.0f, 200.0f},
            .radius = 6.0f,
            .color = {0.20f, 0.90f, 0.30f, 0.50f},
        });
    });
    s.layer("pedestal_card_blue", [](LayerBuilder& l) {
        l.enable_3d();
        l.position({500.0f, -50.0f, 1200.0f});
        l.rounded_rect("card_blue_rect", {
            .size  = {300.0f, 200.0f},
            .radius = 6.0f,
            .color = {0.20f, 0.45f, 0.95f, 0.50f},
        });
    });
}

// Helper: identical text label at a given depth with a chosen font size.
// Each layer shows its own depth-tag so the test is legibly interpretable.
chronon3d::TextSpec text_label(const std::string& word, f32 font_size) {
    return chronon3d::content::text::centered_text({
        .text        = word,
        .box         = {1400.0f, 200.0f},
        .font_size   = font_size,
        .tracking    = 6.0f,
        .color       = {1.0f, 1.0f, 1.0f, 1.0f},
        .line_height = 1.10f,
    });
}

void add_text_stack(SceneBuilder& s, const FrameContext& ctx) {
    // Five text layers parked at five different Z depths.  The user's
    // spec asks for five depths: -500, -250, 0, +300, +700.  Font size
    // is also scaled with depth to give a pre-DOF preview that already
    // reads as depth-ordered.
    //
    // In the stress segment (frames 300–359) `text_near` is yanked past
    // the camera — past the near plane — to expose clipping or
    // perspective-divide artefacts.
    const Frame f = ctx.frame;

    auto near_z = AnimatedValue<float>{-500.0f};
    near_z.key(Frame{300}, -500.0f)
           .key(Frame{330}, -1200.0f)   // past near plane
           .key(Frame{359}, -1500.0f);  // well inside camera

    // Each text layer is named after its depth (NEAR / FRONT / FOCUS /
    // BACK / FAR) so the rendered frame is legible: a viewer can
    // immediately tell which label is rendered large (close) and which
    // is small (far) without having to correlate against metadata.
    s.layer("text_near", [&](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, -30.0f, near_z.evaluate(f)});
        l.text("lbl_near", text_label("NEAR", 140.0f));
    });
    s.layer("text_front", [&](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, -15.0f, -250.0f});
        l.text("lbl_front", text_label("FRONT", 130.0f));
    });
    s.layer("text_focus", [&](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 0.0f});
        l.text("lbl_focus", text_label("FOCUS", 120.0f));
    });
    s.layer("text_back", [&](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 15.0f, 300.0f});
        l.text("lbl_back", text_label("BACK", 110.0f));
    });
    s.layer("text_far", [&](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 30.0f, 700.0f});
        l.text("lbl_far", text_label("FAR", 100.0f));
    });
}

void add_targets(SceneBuilder& s) {
    // `camera_target` is the TwoNode look-at point, parked at the
    // origin.  Even though we evaluate the camera manually (no rig
    // lookup), keeping these null-equivalent layers makes the scene
    // compatible with compositions that switch to a `CameraShotProfile`.
    s.layer("camera_target", [](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 0.0f});
        l.circle("c_dot", {.radius = 6.0f, .color = {1.0f, 0.85f, 0.30f, 1.0f}});
    });
    s.layer("focus_target", [](LayerBuilder& l) {
        l.enable_3d();
        l.position({0.0f, 0.0f, 0.0f});
        l.circle("f_dot", {.radius = 4.0f, .color = {0.30f, 1.00f, 0.85f, 1.0f}});
    });
}

// ── Camera evaluation ──────────────────────────────────────────────────────

// Build a `Camera2_5D` for the current frame based on the segment table.
Camera2_5D evaluate_segment_camera(const FrameContext& ctx) {
    const Frame f = ctx.frame;

    // Segment-local animated values.  Keys are placed at the segment
    // boundaries only, so each AnimatedValue is constant within a segment
    // and the interpolated values are hit deterministically at the
    // 60-frame seams.
    auto orbit_radius = AnimatedValue<float>{1200.0f};
    orbit_radius.key(Frame{0},   1200.0f)
                .key(Frame{60},  1200.0f)
                .key(Frame{120}, 1200.0f)
                .key(Frame{180}, 1200.0f)
                .key(Frame{240}, 1200.0f)
                .key(Frame{300}, 1200.0f)
                .key(Frame{360}, 1200.0f);

    // Segment 1 — Dolly zoom reduces radius while zoom widens.  Both
    // return to baseline at the end of the segment so segment 2 starts
    // clean.
    auto dolly_radius = AnimatedValue<float>{1200.0f};
    dolly_radius.key(Frame{60},  1200.0f)
                 .key(Frame{119},   800.0f)
                 .key(Frame{120},  1200.0f);

    auto dolly_zoom = AnimatedValue<float>{1000.0f};
    dolly_zoom.key(Frame{60},  1000.0f)
              .key(Frame{119}, 1500.0f)  // Hitchcock: zoom widens to keep focal subject stable
              .key(Frame{120}, 1000.0f);

    // Segment 2 — TwoNode orbit (yaw ±35°, pitch ±15°).
    auto yaw   = AnimatedValue<float>{0.0f};
    auto pitch = AnimatedValue<float>{0.0f};
    yaw.key(Frame{120},   0.0f)
       .key(Frame{150},  35.0f)
       .key(Frame{179}, -35.0f)
       .key(Frame{180},   0.0f);
    pitch.key(Frame{120},   0.0f)
         .key(Frame{150}, -15.0f)
         .key(Frame{179},  15.0f)
         .key(Frame{180},   0.0f);

    // Segment 3 — Rack focus physical DOF, focus NEAR→FOCUS→FAR.
    auto focus_distance = AnimatedValue<float>{1200.0f};
    focus_distance.key(Frame{180},  500.0f)   // NEAR (z=-500)
                  .key(Frame{210}, 1200.0f)   // FOCUS (z=0)
                  .key(Frame{240}, 1900.0f);  // FAR (z=+700)

    auto f_stop = AnimatedValue<float>{2.8f};
    // Ramp-in to f/1.8 during the rack-focus segment (180–239) and hold
    // there for the rest of the timeline (whip-pan + stress).  Pre-180
    // frames keep the constructor default of f/2.8.  No duplicate keys.
    f_stop.key(Frame{180}, 1.8f)
           .key(Frame{240}, 1.8f)
           .key(Frame{300}, 1.8f)
           .key(Frame{360}, 1.8f);

    // Segment 4 — Whip pan: world-space camera X lerp over the segment.
    auto cam_x = AnimatedValue<float>{0.0f};
    cam_x.key(Frame{240}, -2000.0f)
         .key(Frame{299},  2000.0f)
         .key(Frame{300},     0.0f);

    // Segment 5 — Stress: target Y crosses zero, roll ramps, focal
    // length ramps 24→135mm.
    auto target_y = AnimatedValue<float>{0.0f};
    target_y.key(Frame{300},  -50.0f)
            .key(Frame{330},   30.0f)
            .key(Frame{359},   50.0f);

    auto roll_deg = AnimatedValue<float>{0.0f};
    roll_deg.key(Frame{300},  0.0f)
            .key(Frame{359}, 30.0f);

    auto focal_len_mm = AnimatedValue<float>{50.0f};
    focal_len_mm.key(Frame{300},  24.0f)
                .key(Frame{330},  85.0f)
                .key(Frame{359}, 135.0f);

    // ── Decide which segment is active and dispatch ────────────────────
    Camera2_5D cam;
    cam.enabled = true;
    cam.is_animated = true;

    // Lens is always 36×24 (full-frame stills); f_stop and focal_length
    // move per segment.  optics_mode is PhysicalLens for all segments so
    // focal_from_camera branches into the physical model.
    cam.optics_mode = CameraOpticsMode::PhysicalLens;
    cam.lens.sensor_width  = 36.0f;
    cam.lens.sensor_height = 24.0f;
    cam.lens.focal_length  = focal_len_mm.evaluate(f);
    cam.lens.f_stop        = f_stop.evaluate(f);
    cam.lens.gate_fit      = GateFit::Fill;

    // Vertical FOV stable at 50° for the dolly/orbit baseline.  Stress
    // segment lets focal_length ramp, which changes the focal_px and
    // therefore effective FOV — exactly the "AE lens swap" behaviour
    // we want under test.
    cam.fov_deg = 50.0f;

    // Target lock — the user spec says target stays at origin for
    // segments 0–3 and crosses vertically only for the stress segment
    // (300+).  For TwoNode mode we set the rig's look-at target.
    cam.point_of_interest_enabled = true;
    cam.point_of_interest = Vec3{0.0f, target_y.evaluate(f), 0.0f};

    // Roll — fed straight into the rotation vector's Z channel.
    cam.rotation = Vec3{
        pitch.evaluate(f),   // tilt (X)
        yaw.evaluate(f),     // pan  (Y)
        roll_deg.evaluate(f) // roll (Z)
    };

    // Position — composed from orbit radius around the target plus the
    // whip-pan X offset (segment 4) and the per-segment radius overrides.
    const float r             = dolly_radius.evaluate(f);
    const float orbit_r       = orbit_radius.evaluate(f);
    const float yaw_rad       = glm::radians(yaw.evaluate(f));
    const float pitch_rad     = glm::radians(pitch.evaluate(f));
    const float effective_r   = (f >= 60 && f < 120) ? r : orbit_r;

    // Orbit position around the target.  During all segments yaw/pitch
    // are the only drivers of this vector — the whip-pan X offset is
    // added on top so frames 240–299 actually translate the camera
    // sideways rather than dolly in/out (which would be a totally
    // different test).
    Vec3 orbit_pos{
        effective_r * std::sin(yaw_rad) * std::cos(pitch_rad),
        effective_r * std::sin(pitch_rad),
        -effective_r * std::cos(yaw_rad) * std::cos(pitch_rad),
    };
    cam.position = Vec3{
        orbit_pos.x + cam_x.evaluate(f),                // whip-pan X offset
        orbit_pos.y + cam.point_of_interest.y,           // orbit around movable target Y
        orbit_pos.z,                                     // Z untouched
    };

    // Zoom (AnimatableValue used by the projection pipeline).
    cam.zoom = dolly_zoom.evaluate(f);

    // ── Segment-specific overlay state ─────────────────────────────────
    // DOF — only active in segment 3 (180–239).
    cam.dof.enabled          = (f >= 180 && f < 240);
    cam.dof.use_physical_model = (f >= 180 && f < 240);
    cam.dof.focus_distance   = focus_distance.evaluate(f);
    cam.dof.aperture         = 0.015f;   // base value; PhysicalModel re-derives
    cam.dof.max_blur         = (f >= 180 && f < 240) ? 30.0f : 24.0f;

    // Motion blur — only active in segment 4 (240–299).
    // TICKET-026 — `motion_blur.enabled` removed; mode is the canonical
    // active-indicator.  Map the legacy `bool enabled` to `TemporalAccumulation`.
    cam.motion_blur.mode              = (f >= 240 && f < 300)
        ? MotionBlurMode::TemporalAccumulation
        : MotionBlurMode::Off;
    cam.motion_blur.samples           = (f >= 240 && f < 300) ? 16 : 8;
    cam.motion_blur.shutter_angle_deg = (f >= 240 && f < 300) ? 180.0f : 0.0f;
    cam.motion_blur.shutter_phase_deg = (f >= 240 && f < 300) ? -90.0f : 0.0f;
    cam.motion_blur.pattern = (f >= 240 && f < 300)
        ? TemporalSamplePattern::Stratified
        : TemporalSamplePattern::Uniform;
    cam.motion_blur.filter = (f >= 240 && f < 300)
        ? TemporalFilter::Gaussian
        : TemporalFilter::Box;
    cam.motion_blur.jitter_seed = 0xABCD1234;

    return cam;
}

void add_hud(SceneBuilder& s, const FrameContext& ctx) {
    // Tiny HUD that prints which segment is active — useful for frame
    // triage without needing to inspect a generated report.  Always
    // renders (opacity low enough not to mask the text subject).
    const Frame f = ctx.frame;
    const char* seg_name = "static";
    if      (f >= 60  && f < 120) seg_name = "dolly_zoom";
    else if (f >= 120 && f < 180) seg_name = "orbit";
    else if (f >= 180 && f < 240) seg_name = "rack_focus";
    else if (f >= 240 && f < 300) seg_name = "whip_pan";
    else if (f >= 300)            seg_name = "stress";

    s.layer("hud_seg", [seg_name](LayerBuilder& l) {
        l.pin_to(chronon3d::Anchor::TopLeft, 24.0f);
        l.opacity(0.85f);
        l.text("hud_label", chronon3d::TextSpec{
            .content    = {.value = std::string("AE-CAMERA PARITY  |  segment: ") + seg_name},
            .font       = {.font_family = "Inter", .font_weight = 700, .font_size = 22.0f},
            .layout     = {.box = {900.0f, 32.0f}, .align = chronon3d::TextAlign::Left,
                            .line_height = 1.20f, .tracking = 3.0f},
            .appearance = {.color = chronon3d::Color{0.85f, 0.90f, 1.00f, 1.0f}},
            .position   = {0.0f, 0.0f, 0.0f},
        });
    });

    s.layer("hud_frame", [f](LayerBuilder& l) {
        l.pin_to(chronon3d::Anchor::TopRight, 24.0f);
        l.opacity(0.65f);
        l.text("hud_f", chronon3d::TextSpec{
            .content    = {.value = "frame=" + std::to_string(static_cast<int>(f))},
            .font       = {.font_family = "Inter", .font_weight = 600, .font_size = 18.0f},
            .layout     = {.box = {260.0f, 24.0f}, .align = chronon3d::TextAlign::Right,
                            .line_height = 1.20f, .tracking = 2.0f},
            .appearance = {.color = chronon3d::Color{0.75f, 0.78f, 0.95f, 1.0f}},
            .position   = {0.0f, 0.0f, 0.0f},
        });
    });
}

} // namespace

// ════════════════════════════════════════════════════════════════════════════
// Composition entry point
// ════════════════════════════════════════════════════════════════════════════

Composition ae_camera_text_parity() {
    return composition({
        .name     = "AECameraTextParity",
        .width    = 1920,
        .height   = 1080,
        .duration = 360,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        add_background(s);
        add_floor_grid(s);
        add_origin_axes(s);
        add_pedestal_cards(s);
        add_text_stack(s, ctx);
        add_targets(s);
        add_hud(s, ctx);

        s.camera().set(evaluate_segment_camera(ctx));

        return s.build();
    });
}

} // namespace chronon3d::content::anims
