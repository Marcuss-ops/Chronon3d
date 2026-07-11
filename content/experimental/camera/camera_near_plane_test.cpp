#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/model/camera/camera_shot_profile.hpp>
#include <chronon3d/math/color.hpp>
#include "camera_calibration_scene.hpp"
#include "camera_test_orchestrator.hpp"

namespace chronon3d::content::two_point_five_d {

// ── CameraNearPlaneCrossingTest ──────────────────────────────────────────
// A card at Z=0 starts in front of the camera (normal). Another card animates
// from behind the camera through the near plane:
//   Frame 0:  card at Z=+500  (behind camera in Y-down → far in view space)
//   Frame 45: card at Z=-50   (near-plane crossing, clipped)
//   Frame 90: card at Z=-500  (very close to camera, large perspective)
//
// The test validates:
//   - No NaN/infinite values from division by zero
//   - Smooth transition across near plane
//   - Polygon clipping works (limited visible_ratio at crossing)
//   - No explosion in projected size
Composition camera_near_plane_crossing_test() {
    return composition({.name = "CameraNearPlaneCrossingTest", .width = 1920, .height = 1080, .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s, false); // no pillars

        // Stationary card at Z=0 (reference, always visible)
        s.layer("card_reference", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("ref_body", {
                .size = {300.0f, 200.0f},
                .radius = 10.0f,
                .color = {0.08f, 0.10f, 0.18f, 1.0f},
                .stroke = {.enabled = true, .color = {0.0f, 0.85f, 1.0f, 0.5f}, .width = 2.0f}
            });
            l.text("ref_lbl", TextSpec{.content = {.value = "REFERENCE (Z=0)"},.position = {0.0f, 0.0f, 0.0f},.font = {.font_size = 18.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = {0.75f, 0.85f, 1.0f, 0.85f}},});
        });

        // Animated card: moves from Z=+500 (behind) to Z=-500 (very close)
        s.layer("card_crossing", [ctx](LayerBuilder& l) {
            const float t = ctx.progress(); // 0→1
            // Z animates from +500 → -500 (behind camera → very close)
            // Using ease-in-out for smooth visual
            const float z = 500.0f - t * 1000.0f; // +500 → -500
            l.enable_3d().position({250.0f, 0.0f, z});

            // Color changes based on depth region
            Color card_col;
            if (z > 100.0f)
                card_col = {0.6f, 0.2f, 0.2f, 1.0f};    // BEHIND = red
            else if (z > -10.0f)
                card_col = {0.6f, 0.6f, 0.1f, 1.0f};    // CROSSING = yellow
            else
                card_col = {0.1f, 0.5f, 0.3f, 1.0f};    // CLOSE = green

            l.rounded_rect("cross_body", {
                .size = {280.0f, 190.0f},
                .radius = 10.0f,
                .color = card_col,
                .stroke = {.enabled = true, .color = {1.0f, 1.0f, 1.0f, 0.4f}, .width = 2.0f}
            });

            char z_buf[64];
            snprintf(z_buf, sizeof(z_buf), "Z=%.0f", z);
            l.text("cross_lbl", TextSpec{.content = {.value = std::string(z_buf)},.position = {0.0f, 0.0f, 0.0f},.font = {.font_size = 16.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = {1.0f, 1.0f, 1.0f, 0.9f}},});

            // Depth zone label
            const char* zone;
            if (z > 100.0f)      zone = "BEHIND CAMERA";
            else if (z > -10.0f) zone = "NEAR-PLANE CROSSING";
            else                 zone = "CLOSE (large)";
            l.text("zone_lbl", TextSpec{.content = {.value = zone},.position = {0.0f, 0.0f, 0.0f},.font = {.font_size = 11.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = {1.0f, 1.0f, 1.0f, 0.6f}},});
        });

        // Frame counter HUD
        s.layer("frame_hud", [ctx](LayerBuilder& l) {
            l.pin_to(Anchor::TopLeft, 20.0f);
            char buf[64];
            snprintf(buf, sizeof(buf), "Frame %d / 90", static_cast<int>(ctx.frame));
            l.text("hud", TextSpec{.content = {.value = buf},.position = {0.0f, 0.0f, 0.0f},.font = {.font_size = 18.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {0.6f, 0.6f, 0.6f, 0.7f}},});
        });

        // Camera: stationary, front-facing
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.orbit_yaw.set(0.0f);
        shot.rig.orbit_pitch.set(0.0f);
        shot.rig.roll.set(0.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("card_reference", {300.0f, 200.0f})
            .register_layer_size("card_crossing", {280.0f, 190.0f})
            .require_visible("card_reference", 0.60f);

        return camera_test_orchestrator(ctx, s, shot, "CameraNearPlaneCrossingTest",
            {"card_reference", "card_crossing"}, {0, 30, 45, 60, 90});
    });
}

} // namespace chronon3d::content::two_point_five_d
