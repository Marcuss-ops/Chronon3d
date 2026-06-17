#include "camera_advanced_tests.hpp"
#include "camera_test_orchestrator.hpp"
#include "camera_calibration_scene.hpp"
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/model/camera/camera_shot_profile.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::content::two_point_five_d {

// ═════════════════════════════════════════════════════════════════════════════
// Camera rig tests — orbit target lock, dolly perspective scale,
// parent null rig, roll/pan/tilt grid, safe framing aspect ratios.
// ═════════════════════════════════════════════════════════════════════════════

Composition camera_orbit_target_lock_test() {
    return composition({.name = "CameraOrbitTargetLockTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_yaw.key(0, -25.0f).key(90, 25.0f, EasingCurve{Easing::InOutCubic});
        shot.rig.orbit_pitch.key(0, -5.0f).key(90, 5.0f);
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("pillar_near", {60.0f, 380.0f}).register_layer_size("pillar_mid", {60.0f, 300.0f})
            .register_layer_size("pillar_far", {50.0f, 220.0f})
            .require_target_centered("camera_target", 3.0f).require_visible("calibration_card", 0.80f)
            .require_visible("pillar_near", 0.50f).require_visible("pillar_far", 0.30f);
        return camera_test_orchestrator(ctx, s, shot, "CameraOrbitTargetLockTest", calibration_landmark_layers(), {0, 45, 90});
    });
}

Composition camera_dolly_perspective_scale_test() {
    return composition({.name = "CameraDollyPerspectiveScaleTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.key(0, 1600.0f).key(45, 800.0f, EasingCurve{Easing::InOutCubic}).key(90, 1600.0f, EasingCurve{Easing::InOutCubic});
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("pillar_near", {60.0f, 380.0f}).register_layer_size("pillar_mid", {60.0f, 300.0f})
            .register_layer_size("pillar_far", {50.0f, 220.0f})
            .require_target_centered("camera_target", 3.0f).require_visible("calibration_card", 0.80f)
            .require_visible("pillar_near", 0.50f).require_visible("pillar_far", 0.30f)
            .require_depth_order({"pillar_near", "calibration_card", "pillar_far"});
        return camera_test_orchestrator(ctx, s, shot, "CameraDollyPerspectiveScaleTest", calibration_landmark_layers(), {0, 45, 90});
    });
}

Composition camera_parent_null_rig_test() {
    return composition({.name = "CameraParentNullRigTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg_fill", [](LayerBuilder& l) { l.pin_to(Anchor::Center); l.rect("bg", {.size = {1920.0f, 1080.0f}, .color = {0.04f, 0.04f, 0.06f, 1.0f}, .pos = {0.0f, 0.0f, -10.0f}}); });
        s.null_layer("camera_rig_null", [ctx](NullBuilder& n) { n.position({0.0f, 0.0f, 0.0f}); n.rotation({0.0f, ctx.progress() * 20.0f, 0.0f}); });
        s.null_layer("card_null", [](NullBuilder& n) { n.position({0.0f, 0.0f, -100.0f}); n.parent("camera_rig_null"); });
        s.null_layer("camera_target", [](NullBuilder& n) { n.position({0.0f, 0.0f, 0.0f}); n.parent("camera_rig_null"); });
        s.layer("card_a", [](LayerBuilder& l) { l.enable_3d().position({80.0f, 0.0f, 0.0f}).parent("card_null"); l.rounded_rect("rect_a", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.9f}}); l.text("lbl_a", {.text = "A", .pos = {20.0f, 30.0f, 0.0f}}); });
        s.layer("card_b", [](LayerBuilder& l) { l.enable_3d().position({-80.0f, 0.0f, 0.0f}).parent("card_null"); l.rounded_rect("rect_b", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.99f, 0.44f, 0.82f, 0.9f}}); l.text("lbl_b", {.text = "B", .pos = {20.0f, 30.0f, 0.0f}}); });
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.parent_name = "camera_rig_null";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("card_a", {200.0f, 150.0f}).register_layer_size("card_b", {200.0f, 150.0f})
            .require_target_centered("camera_target", 3.0f).require_visible("card_a", 0.70f).require_visible("card_b", 0.70f);
        return camera_test_orchestrator(ctx, s, shot, "CameraParentNullRigTest", {"card_a", "card_b"}, {0, 45, 90});
    });
}

Composition camera_roll_pan_tilt_grid_test() {
    return composition({.name = "CameraRollPanTiltGridTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.roll.key(0, 0.0f).key(30, 10.0f).key(90, 10.0f);
        shot.rig.orbit_pitch.key(0, 0.0f).key(30, 0.0f).key(60, 15.0f).key(90, 15.0f);
        shot.rig.orbit_yaw.key(0, 0.0f).key(60, 0.0f).key(90, 20.0f);
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f})
            .require_target_centered("camera_target", 3.0f).require_visible("calibration_card", 0.70f);
        return camera_test_orchestrator(ctx, s, shot, "CameraRollPanTiltGridTest", calibration_landmark_layers(), {0, 30, 60, 90});
    });
}

static Scene camera_safe_framing_aspect_ratio_test_impl(const FrameContext& ctx, int, int, const std::string& comp_name) {
    SceneBuilder s(ctx);
    add_camera_calibration_scene(s);
    CameraShotProfile shot;
    shot.rig = camera_rig_presets::orbit_reveal("camera_target");
    shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f})
        .register_layer_size("pillar_near", {60.0f, 380.0f}).register_layer_size("pillar_mid", {60.0f, 300.0f})
        .register_layer_size("pillar_far", {50.0f, 220.0f})
        .require_inside_safe_area("calibration_card", 0.08f).require_visible("calibration_card", 0.95f);
    shot.auto_fit = true;
    shot.framing.max_iterations = 30;
    shot.framing.dolly_step = 120.0f;
    return camera_test_orchestrator(ctx, s, shot, comp_name, calibration_landmark_layers(), {0, 45, 90});
}

Composition camera_safe_framing_aspect_ratio_16_9() {
    return composition({.name = "CameraSafeFramingAspectRatioTest_16_9", .width = 1920, .height = 1080, .duration = 91}, [](const FrameContext& ctx) {
        return camera_safe_framing_aspect_ratio_test_impl(ctx, 1920, 1080, "CameraSafeFramingAspectRatioTest_16_9");
    });
}
Composition camera_safe_framing_aspect_ratio_1_1() {
    return composition({.name = "CameraSafeFramingAspectRatioTest_1_1", .width = 1080, .height = 1080, .duration = 91}, [](const FrameContext& ctx) {
        return camera_safe_framing_aspect_ratio_test_impl(ctx, 1080, 1080, "CameraSafeFramingAspectRatioTest_1_1");
    });
}
Composition camera_safe_framing_aspect_ratio_9_16() {
    return composition({.name = "CameraSafeFramingAspectRatioTest_9_16", .width = 1080, .height = 1920, .duration = 91}, [](const FrameContext& ctx) {
        return camera_safe_framing_aspect_ratio_test_impl(ctx, 1080, 1920, "CameraSafeFramingAspectRatioTest_9_16");
    });
}
Composition camera_safe_framing_aspect_ratio_4_5() {
    return composition({.name = "CameraSafeFramingAspectRatioTest_4_5", .width = 1080, .height = 1350, .duration = 91}, [](const FrameContext& ctx) {
        return camera_safe_framing_aspect_ratio_test_impl(ctx, 1080, 1350, "CameraSafeFramingAspectRatioTest_4_5");
    });
}

} // namespace chronon3d::content::two_point_five_d
