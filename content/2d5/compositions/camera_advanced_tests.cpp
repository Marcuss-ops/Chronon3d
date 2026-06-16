#include "camera_advanced_tests.hpp"
#include "camera_test_orchestrator.hpp"
#include "camera_calibration_scene.hpp"
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/model/camera/camera_shot_profile.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>

namespace chronon3d::content::two_point_five_d {

// ─────────────────────────────────────────────────────────────────────────────
// 1. FrustumCullingPrecisionTest
// ─────────────────────────────────────────────────────────────────────────────
Composition camera_frustum_culling_precision_test() {
    return composition({.name = "CameraFrustumCullingPrecisionTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);

        // Override camera_target to animate during test
        s.null_layer("camera_target", [ctx](NullBuilder& n) {
            n.position({(ctx.progress() - 0.5f) * 3000.0f, 0.0f, 0.0f});
        });

        // 10×10 grid of cards across a range of Z depths for frustum culling
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < 10; ++x) {
                std::string name = "card_" + std::to_string(x) + "_" + std::to_string(y);
                float px = (x - 4.5f) * 350.0f;
                float py = (y - 4.5f) * 200.0f;
                float pz = (x - 4.5f) * 80.0f;
                s.layer(name, [px, py, pz](LayerBuilder& l) {
                    l.enable_3d().position({px, py, pz});
                    l.rounded_rect("rect", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.9f}});
                });
            }
        }

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.set(1200.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f});
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < 10; ++x) {
                std::string name = "card_" + std::to_string(x) + "_" + std::to_string(y);
                shot.validator.register_layer_size(name, {200.0f, 150.0f});
                shot.validator.require_visible(name, 0.0f);
            }
        }

        return camera_test_orchestrator(ctx, s, shot, "CameraFrustumCullingPrecisionTest", {}, {0, 45, 90});
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. KinematicJerkAndInterpolationTest
// ─────────────────────────────────────────────────────────────────────────────
Composition camera_kinematic_jerk_interpolation_test() {
    return composition({.name = "CameraKinematicJerkAndInterpolationTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);

        // Animated target for jerk measurement
        s.null_layer("camera_target", [ctx](NullBuilder& n) {
            float t = ctx.progress();
            float px = 600.0f * std::sin(t * 3.14159f * 2.0f);
            float py = 300.0f * std::cos(t * 3.14159f * 4.0f);
            float pz = 200.0f * std::sin(t * 3.14159f * 3.0f);
            n.position({px, py, pz});
        });

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.key(0, 1000.0f).key(45, 600.0f, EasingCurve{Easing::InOutCubic}).key(90, 1000.0f, EasingCurve{Easing::InOutCubic});
        shot.rig.orbit_yaw.key(0, 0.0f).key(45, 45.0f).key(90, -45.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f});

        return camera_test_orchestrator(ctx, s, shot, "CameraKinematicJerkAndInterpolationTest", {}, {0, 45, 90});
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. DepthSortingStressTest
// ─────────────────────────────────────────────────────────────────────────────
Composition camera_depth_sorting_stress_test() {
    return composition({.name = "CameraDepthSortingStressTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);

        // 5 semi-transparent cards at microscopical depth intervals
        // Overlapping the calibration card at Z ≅ 0
        s.layer("card_1", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 10.001f});
            l.rounded_rect("r1", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{0.99f, 0.44f, 0.82f, 0.5f}});
        });
        s.layer("card_2", [](LayerBuilder& l) {
            l.enable_3d().position({20.0f, 10.0f, 10.002f});
            l.rounded_rect("r2", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.5f}});
        });
        s.layer("card_3", [](LayerBuilder& l) {
            l.enable_3d().position({40.0f, 20.0f, 10.003f});
            l.rounded_rect("r3", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{0.2f, 0.9f, 0.2f, 0.5f}});
        });
        s.layer("card_4", [](LayerBuilder& l) {
            l.enable_3d().position({60.0f, 30.0f, 10.004f});
            l.rounded_rect("r4", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{1.0f, 0.8f, 0.0f, 0.5f}});
        });
        s.layer("card_5", [](LayerBuilder& l) {
            l.enable_3d().position({80.0f, 40.0f, 10.005f});
            l.rounded_rect("r5", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{1.0f, 0.2f, 0.2f, 0.5f}});
        });

        // Failure cases
        s.layer("card_same_z_a", [](LayerBuilder& l) {
            l.enable_3d().position({-100.0f, -50.0f, 20.0f});
            l.rounded_rect("r_same_a", {.size = {150.0f, 100.0f}, .radius = 4.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.5f}});
        });
        s.layer("card_same_z_b", [](LayerBuilder& l) {
            l.enable_3d().position({-100.0f, -50.0f, 20.0f});
            l.rounded_rect("r_same_b", {.size = {150.0f, 100.0f}, .radius = 4.0f, .color = Color{0.99f, 0.44f, 0.82f, 0.5f}});
        });

        s.layer("card_intersect_a", [](LayerBuilder& l) {
            l.enable_3d().position({100.0f, 50.0f, 30.0f}).rotate({0.0f, 45.0f, 0.0f});
            l.rounded_rect("r_int_a", {.size = {150.0f, 100.0f}, .radius = 4.0f, .color = Color{0.2f, 0.9f, 0.2f, 0.5f}});
        });
        s.layer("card_intersect_b", [](LayerBuilder& l) {
            l.enable_3d().position({100.0f, 50.0f, 30.0f}).rotate({0.0f, -45.0f, 0.0f});
            l.rounded_rect("r_int_b", {.size = {150.0f, 100.0f}, .radius = 4.0f, .color = Color{1.0f, 0.8f, 0.0f, 0.5f}});
        });

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_yaw.key(0, -30.0f).key(90, 30.0f);
        shot.rig.orbit_pitch.key(0, -15.0f).key(90, 15.0f);
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("card_1", {300.0f, 200.0f})
            .register_layer_size("card_2", {300.0f, 200.0f})
            .register_layer_size("card_3", {300.0f, 200.0f})
            .register_layer_size("card_4", {300.0f, 200.0f})
            .register_layer_size("card_5", {300.0f, 200.0f})
            .register_layer_size("card_same_z_a", {150.0f, 100.0f})
            .register_layer_size("card_same_z_b", {150.0f, 100.0f})
            .register_layer_size("card_intersect_a", {150.0f, 100.0f})
            .register_layer_size("card_intersect_b", {150.0f, 100.0f})
            .require_depth_order({"card_1", "card_2", "card_3", "card_4", "card_5"});

        return camera_test_orchestrator(ctx, s, shot, "CameraDepthSortingStressTest", {}, {0, 45, 90});
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. SubpixelJitterValidationTest
// ─────────────────────────────────────────────────────────────────────────────
Composition camera_subpixel_jitter_validation_test() {
    return composition({.name = "CameraSubpixelJitterValidationTest", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        float current_frame = static_cast<float>(ctx.frame);
        shot.rig.orbit_yaw.set(current_frame * 0.01f);
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .require_visible("calibration_card", 0.0f);

        return camera_test_orchestrator(ctx, s, shot, "CameraSubpixelJitterValidationTest", {}, {0, 45, 90, 119});
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. FitTargetBoundingBoxFitTest (MultiTargetBoundingBoxFitTest)
// ─────────────────────────────────────────────────────────────────────────────
Composition camera_multi_target_bounding_box_fit_test() {
    return composition({.name = "CameraMultiTargetBoundingBoxFitTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);

        // Three animated cards for multi-target fitting
        s.layer("card_a", [ctx](LayerBuilder& l) {
            float dx = ctx.progress() * 250.0f;
            l.enable_3d().position({dx, 0.0f, 0.0f});
            l.rounded_rect("r_a", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.9f}});
        });

        s.layer("card_b", [ctx](LayerBuilder& l) {
            float dx = -ctx.progress() * 250.0f;
            l.enable_3d().position({dx, 0.0f, 0.0f});
            l.rounded_rect("r_b", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.99f, 0.44f, 0.82f, 0.9f}});
        });

        s.layer("card_c", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 100.0f, 0.0f});
            l.rounded_rect("r_c", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.2f, 0.9f, 0.2f, 0.9f}});
        });

        CameraShotProfile shot;
        shot.rig = camera_rig_presets::orbit_reveal("camera_target");
        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("card_a", {200.0f, 150.0f})
            .register_layer_size("card_b", {200.0f, 150.0f})
            .register_layer_size("card_c", {200.0f, 150.0f})
            .require_inside_safe_area("card_a", 0.08f)
            .require_inside_safe_area("card_b", 0.08f)
            .require_inside_safe_area("card_c", 0.08f)
            .require_visible("card_a", 0.95f)
            .require_visible("card_b", 0.95f)
            .require_visible("card_c", 0.95f);
        shot.auto_fit = true;

        return camera_test_orchestrator(ctx, s, shot, "CameraMultiTargetBoundingBoxFitTest",
            {"card_a", "card_b", "card_c"}, {0, 45, 90});
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. CameraOrbitTargetLockTest
// ─────────────────────────────────────────────────────────────────────────────
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

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("pillar_near", {60.0f, 380.0f})
            .register_layer_size("pillar_mid", {60.0f, 300.0f})
            .register_layer_size("pillar_far", {50.0f, 220.0f})
            .require_target_centered("camera_target", 3.0f)
            .require_visible("calibration_card", 0.80f)
            .require_visible("pillar_near", 0.50f)
            .require_visible("pillar_far", 0.30f);

        return camera_test_orchestrator(ctx, s, shot, "CameraOrbitTargetLockTest",
            calibration_landmark_layers(), {0, 45, 90});
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. CameraDollyPerspectiveScaleTest
// ─────────────────────────────────────────────────────────────────────────────
Composition camera_dolly_perspective_scale_test() {
    return composition({.name = "CameraDollyPerspectiveScaleTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.key(0, 1600.0f)
                              .key(45, 800.0f, EasingCurve{Easing::InOutCubic})
                              .key(90, 1600.0f, EasingCurve{Easing::InOutCubic});
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("pillar_near", {60.0f, 380.0f})
            .register_layer_size("pillar_mid", {60.0f, 300.0f})
            .register_layer_size("pillar_far", {50.0f, 220.0f})
            .require_target_centered("camera_target", 3.0f)
            .require_visible("calibration_card", 0.80f)
            .require_visible("pillar_near", 0.50f)
            .require_visible("pillar_far", 0.30f)
            .require_depth_order({"pillar_near", "calibration_card", "pillar_far"});

        return camera_test_orchestrator(ctx, s, shot, "CameraDollyPerspectiveScaleTest",
            calibration_landmark_layers(), {0, 45, 90});
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. CameraParentNullRigTest
// ─────────────────────────────────────────────────────────────────────────────
Composition camera_parent_null_rig_test() {
    return composition({.name = "CameraParentNullRigTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);

        // Parent null that rotates — camera_rig_null, camera_target
        // and card_null all share this parent hierarchy
        s.null_layer("camera_rig_null", [ctx](NullBuilder& n) {
            n.position({0.0f, 0.0f, 0.0f});
            n.rotation({0.0f, ctx.progress() * 20.0f, 0.0f});
        });

        s.null_layer("card_null", [](NullBuilder& n) {
            n.position({0.0f, 0.0f, -100.0f});
            n.parent("camera_rig_null");
        });

        // Reparent camera_target under the rig null
        s.null_layer("camera_target", [](NullBuilder& n) {
            n.position({0.0f, 0.0f, 0.0f});
            n.parent("camera_rig_null");
        });

        // Two animated child cards under card_null
        s.layer("card_a", [](LayerBuilder& l) {
            l.enable_3d().position({80.0f, 0.0f, 0.0f}).parent("card_null");
            l.rounded_rect("rect_a", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.9f}});
            l.text("lbl_a", {.text = "A", .pos = {20.0f, 30.0f, 0.0f}});
        });

        s.layer("card_b", [](LayerBuilder& l) {
            l.enable_3d().position({-80.0f, 0.0f, 0.0f}).parent("card_null");
            l.rounded_rect("rect_b", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.99f, 0.44f, 0.82f, 0.9f}});
            l.text("lbl_b", {.text = "B", .pos = {20.0f, 30.0f, 0.0f}});
        });

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.parent_name = "camera_rig_null";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("card_a", {200.0f, 150.0f})
            .register_layer_size("card_b", {200.0f, 150.0f})
            .require_target_centered("camera_target", 3.0f)
            .require_visible("card_a", 0.70f)
            .require_visible("card_b", 0.70f);

        return camera_test_orchestrator(ctx, s, shot, "CameraParentNullRigTest",
            {"card_a", "card_b"}, {0, 45, 90});
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. CameraRollPanTiltGridTest
// ─────────────────────────────────────────────────────────────────────────────
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

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .require_target_centered("camera_target", 3.0f)
            .require_visible("calibration_card", 0.70f);

        return camera_test_orchestrator(ctx, s, shot, "CameraRollPanTiltGridTest",
            calibration_landmark_layers(), {0, 30, 60, 90});
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. SafeFramingAspectRatioTest implementations
// ─────────────────────────────────────────────────────────────────────────────
Scene camera_safe_framing_aspect_ratio_test_impl(const FrameContext& ctx, i32 /*width*/, i32 /*height*/, const std::string& comp_name) {
    SceneBuilder s(ctx);
    add_camera_calibration_scene(s);

    CameraShotProfile shot;
    shot.rig = camera_rig_presets::orbit_reveal("camera_target");
    shot.validator
        .register_layer_size("calibration_card", {500.0f, 350.0f})
        .register_layer_size("pillar_near", {60.0f, 380.0f})
        .register_layer_size("pillar_mid", {60.0f, 300.0f})
        .register_layer_size("pillar_far", {50.0f, 220.0f})
        .require_inside_safe_area("calibration_card", 0.08f)
        .require_visible("calibration_card", 0.95f);
    shot.auto_fit = true;
    shot.framing.max_iterations = 30;
    shot.framing.dolly_step = 120.0f;

    return camera_test_orchestrator(ctx, s, shot, comp_name,
        calibration_landmark_layers(), {0, 45, 90});
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

// ─────────────────────────────────────────────────────────────────────────────
// 11. CameraDepthPerspectiveScaleDiagnosticTest
//     Calibration scene + layers at 5 distinct Z depths with drop lines
// ─────────────────────────────────────────────────────────────────────────────
Composition camera_depth_perspective_scale_diagnostic_test() {
    return composition({.name = "CameraDepthPerspectiveScaleDiagnosticTest", .width = 1920, .height = 1080, .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);

        // ── 5 depth cards overlay — positioned at different Z depths ───────
        s.layer("depth_far", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({-100.0f, 0.0f, 400.0f});
            l.rounded_rect("bg", {.size = {300.0f, 180.0f}, .radius = 14.0f, .color = Color{0.06f, 0.09f, 0.22f, 0.85f},
                .stroke = {.enabled = true, .color = Color{0.12f, 0.22f, 0.55f, 0.40f}, .width = 1.5f}});
            l.text("label", {.text = "FAR (Z=+400)", .pos = {0.0f, -8.0f, 0.1f}, .font_size = 18.0f, .color = Color{0.50f, 0.65f, 0.90f, 0.85f}, .align = TextAlign::Center});
            l.text("desc", {.text = "Smallest on screen", .pos = {0.0f, 14.0f, 0.1f}, .font_size = 11.0f, .color = Color{0.40f, 0.50f, 0.75f, 0.55f}, .align = TextAlign::Center});
        });

        s.layer("depth_mid_far", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({350.0f, -30.0f, 200.0f});
            l.rounded_rect("bg", {.size = {250.0f, 150.0f}, .radius = 12.0f, .color = Color{0.10f, 0.15f, 0.38f, 0.88f},
                .stroke = {.enabled = true, .color = Color{0.18f, 0.38f, 0.82f, 0.50f}, .width = 2.0f}});
            l.text("label", {.text = "MID-FAR (Z=+200)", .pos = {0.0f, -6.0f, 0.1f}, .font_size = 16.0f, .color = Color{0.55f, 0.72f, 1.0f, 0.90f}, .align = TextAlign::Center});
            l.text("desc", {.text = "Behind center", .pos = {0.0f, 12.0f, 0.1f}, .font_size = 10.0f, .color = Color{0.50f, 0.60f, 0.85f, 0.55f}, .align = TextAlign::Center});
        });

        s.layer("depth_center", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({-300.0f, 20.0f, 0.0f});
            l.rounded_rect("bg", {.size = {250.0f, 160.0f}, .radius = 12.0f, .color = Color{0.18f, 0.30f, 0.72f, 0.92f},
                .stroke = {.enabled = true, .color = Color{0.30f, 0.55f, 1.0f, 0.60f}, .width = 2.0f}});
            l.text("label", {.text = "CENTER (Z=0)", .pos = {0.0f, -6.0f, 0.1f}, .font_size = 18.0f, .color = Color{0.80f, 0.88f, 1.0f, 1.0f}, .align = TextAlign::Center});
            l.text("desc", {.text = "Reference", .pos = {0.0f, 14.0f, 0.1f}, .font_size = 11.0f, .color = Color{0.65f, 0.75f, 1.0f, 0.65f}, .align = TextAlign::Center});
        });

        s.layer("depth_near", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({200.0f, 60.0f, -200.0f});
            l.rounded_rect("bg", {.size = {240.0f, 140.0f}, .radius = 10.0f, .color = Color{0.04f, 0.28f, 0.52f, 0.92f},
                .stroke = {.enabled = true, .color = Color{0.08f, 0.65f, 0.92f, 0.70f}, .width = 2.0f}});
            l.text("label", {.text = "NEAR (Z=-200)", .pos = {0.0f, -4.0f, 0.1f}, .font_size = 18.0f, .color = Color{0.55f, 0.92f, 1.0f, 1.0f}, .align = TextAlign::Center});
            l.text("desc", {.text = "Larger on screen", .pos = {0.0f, 14.0f, 0.1f}, .font_size = 11.0f, .color = Color{0.45f, 0.82f, 1.0f, 0.70f}, .align = TextAlign::Center});
        });

        s.layer("depth_foreground", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({-200.0f, -40.0f, -400.0f});
            l.rounded_rect("bg", {.size = {220.0f, 130.0f}, .radius = 10.0f, .color = Color{0.02f, 0.42f, 0.75f, 1.0f},
                .stroke = {.enabled = true, .color = Color{0.25f, 0.90f, 1.0f, 0.85f}, .width = 3.0f}});
            l.text("label", {.text = "FG (Z=-400)", .pos = {0.0f, -4.0f, 0.1f}, .font_size = 18.0f, .color = Color{0.75f, 1.0f, 1.0f, 1.0f}, .align = TextAlign::Center});
            l.text("desc", {.text = "Closest — largest", .pos = {0.0f, 14.0f, 0.1f}, .font_size = 11.0f, .color = Color{0.65f, 0.95f, 1.0f, 0.75f}, .align = TextAlign::Center});
        });

        // ── Vertical drop lines to ground plane ───────────────────────────
        struct DepthCard { const char* name; float px; float py; float z; const char* z_label; };
        DepthCard cards[] = {
            {"depth_foreground", -200.0f, -40.0f, -400.0f, "Z=-400"},
            {"depth_near",        200.0f,  60.0f, -200.0f, "Z=-200"},
            {"depth_center",     -300.0f,  20.0f,    0.0f, "Z=0"},
            {"depth_mid_far",     350.0f, -30.0f,  200.0f, "Z=+200"},
            {"depth_far",        -100.0f,   0.0f,  400.0f, "Z=+400"},
        };
        for (const auto& c : cards) {
            float alpha = 0.20f + 0.15f * (1.0f - std::abs(c.z) / 400.0f);
            float drop_y = 250.0f - c.py;  // distance from card Y to grid plane Y=-250
            s.layer(std::string("drop_") + c.name, [c, alpha, drop_y](LayerBuilder& l) {
                l.enable_3d().position({c.px, c.py, c.z});
                l.line("drop", LineParams{
                    .from = {0.0f, 0.0f, 0.0f},
                    .to = {0.0f, drop_y, 0.0f},
                    .thickness = 1.2f,
                    .color = Color{0.6f, 0.8f, 1.0f, alpha}
                });
                l.circle("base_dot", {
                    .radius = 3.5f,
                    .color = Color{0.6f, 0.8f, 1.0f, std::min(alpha * 1.5f, 1.0f)},
                    .pos = {0.0f, drop_y, 0.1f}
                });
                l.text("z_lbl", {
                    .text = std::string(c.z_label),
                    .pos = {10.0f, drop_y - 4.0f, 0.15f},
                    .font_size = 9.0f,
                    .color = Color{0.55f, 0.75f, 1.0f, alpha * 1.2f},
                    .align = TextAlign::Left
                });
            });
        }

        // ── Camera: dolly from far to near + gentle orbit ────────────────
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";

        // Camera dollies: far (1400) → close (500) → far (1400)
        shot.rig.orbit_radius
            .key(0, 1400.0f)
            .key(22, 900.0f, EasingCurve{Easing::InOutCubic})
            .key(45, 500.0f, EasingCurve{Easing::InOutCubic})
            .key(67, 900.0f, EasingCurve{Easing::InOutCubic})
            .key(90, 1400.0f, EasingCurve{Easing::InOutCubic});

        shot.rig.orbit_yaw
            .key(0, -12.0f)
            .key(45, 12.0f, EasingCurve{Easing::InOutSine})
            .key(90, -12.0f, EasingCurve{Easing::InOutSine});

        shot.rig.orbit_pitch.set(-4.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(54.4f);

        shot.validator
            .register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("depth_far", {300.0f, 180.0f})
            .register_layer_size("depth_mid_far", {250.0f, 150.0f})
            .register_layer_size("depth_center", {250.0f, 160.0f})
            .register_layer_size("depth_near", {240.0f, 140.0f})
            .register_layer_size("depth_foreground", {220.0f, 130.0f})
            .require_depth_order({"depth_foreground", "depth_near", "depth_center", "depth_mid_far", "depth_far"});

        return camera_test_orchestrator(ctx, s, shot, "CameraDepthPerspectiveScaleDiagnosticTest",
            {"depth_far", "depth_mid_far", "depth_center", "depth_near", "depth_foreground"},
            {0, 45, 90});
    });
}

} // namespace chronon3d::content::two_point_five_d
