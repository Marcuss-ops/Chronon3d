#include "camera_advanced_tests.hpp"
#include "camera_test_orchestrator.hpp"
#include "camera_calibration_scene.hpp"
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/model/camera/camera_shot_profile.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>

namespace chronon3d::content::two_point_five_d {

// ═════════════════════════════════════════════════════════════════════════════
// Scene stress tests — frustum culling, kinematic jerk, depth sorting,
// subpixel jitter, multi-target bounding box fit.
// ═════════════════════════════════════════════════════════════════════════════

Composition camera_frustum_culling_precision_test() {
    return composition({.name = "CameraFrustumCullingPrecisionTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);
        s.null_layer("camera_target", [ctx](NullBuilder& n) {
            n.position({(ctx.progress() - 0.5f) * 3000.0f, 0.0f, 0.0f});
        });
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
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f});
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 10; ++x)
                shot.validator.register_layer_size("card_" + std::to_string(x) + "_" + std::to_string(y), {200.0f, 150.0f});
        return camera_test_orchestrator(ctx, s, shot, "CameraFrustumCullingPrecisionTest", {}, {0, 45, 90});
    });
}

Composition camera_kinematic_jerk_interpolation_test() {
    return composition({.name = "CameraKinematicJerkAndInterpolationTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);
        s.null_layer("camera_target", [ctx](NullBuilder& n) {
            float t = ctx.progress();
            n.position({600.0f * std::sin(t * 3.14159f * 2.0f), 300.0f * std::cos(t * 3.14159f * 4.0f), 200.0f * std::sin(t * 3.14159f * 3.0f)});
        });
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.key(0, 1000.0f).key(45, 600.0f, EasingCurve{Easing::InOutCubic}).key(90, 1000.0f, EasingCurve{Easing::InOutCubic});
        shot.rig.orbit_yaw.key(0, 0.0f).key(45, 45.0f).key(90, -45.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f});
        return camera_test_orchestrator(ctx, s, shot, "CameraKinematicJerkAndInterpolationTest", {}, {0, 45, 90});
    });
}

Composition camera_depth_sorting_stress_test() {
    return composition({.name = "CameraDepthSortingStressTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);
        s.layer("card_1", [](LayerBuilder& l) { l.enable_3d().position({0.0f, 0.0f, 10.001f}); l.rounded_rect("r1", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{0.99f, 0.44f, 0.82f, 0.5f}}); });
        s.layer("card_2", [](LayerBuilder& l) { l.enable_3d().position({20.0f, 10.0f, 10.002f}); l.rounded_rect("r2", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.5f}}); });
        s.layer("card_3", [](LayerBuilder& l) { l.enable_3d().position({40.0f, 20.0f, 10.003f}); l.rounded_rect("r3", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{0.2f, 0.9f, 0.2f, 0.5f}}); });
        s.layer("card_4", [](LayerBuilder& l) { l.enable_3d().position({60.0f, 30.0f, 10.004f}); l.rounded_rect("r4", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{1.0f, 0.8f, 0.0f, 0.5f}}); });
        s.layer("card_5", [](LayerBuilder& l) { l.enable_3d().position({80.0f, 40.0f, 10.005f}); l.rounded_rect("r5", {.size = {300.0f, 200.0f}, .radius = 8.0f, .color = Color{1.0f, 0.2f, 0.2f, 0.5f}}); });
        s.layer("card_same_z_a", [](LayerBuilder& l) { l.enable_3d().position({-100.0f, -50.0f, 20.0f}); l.rounded_rect("r_same_a", {.size = {150.0f, 100.0f}, .radius = 4.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.5f}}); });
        s.layer("card_same_z_b", [](LayerBuilder& l) { l.enable_3d().position({-100.0f, -50.0f, 20.0f}); l.rounded_rect("r_same_b", {.size = {150.0f, 100.0f}, .radius = 4.0f, .color = Color{0.99f, 0.44f, 0.82f, 0.5f}}); });
        s.layer("card_intersect_a", [](LayerBuilder& l) { l.enable_3d().position({100.0f, 50.0f, 30.0f}).rotate({0.0f, 45.0f, 0.0f}); l.rounded_rect("r_int_a", {.size = {150.0f, 100.0f}, .radius = 4.0f, .color = Color{0.2f, 0.9f, 0.2f, 0.5f}}); });
        s.layer("card_intersect_b", [](LayerBuilder& l) { l.enable_3d().position({100.0f, 50.0f, 30.0f}).rotate({0.0f, -45.0f, 0.0f}); l.rounded_rect("r_int_b", {.size = {150.0f, 100.0f}, .radius = 4.0f, .color = Color{1.0f, 0.8f, 0.0f, 0.5f}}); });
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_yaw.key(0, -30.0f).key(90, 30.0f);
        shot.rig.orbit_pitch.key(0, -15.0f).key(90, 15.0f);
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("card_1", {300.0f, 200.0f}).register_layer_size("card_2", {300.0f, 200.0f})
            .register_layer_size("card_3", {300.0f, 200.0f}).register_layer_size("card_4", {300.0f, 200.0f})
            .register_layer_size("card_5", {300.0f, 200.0f}).register_layer_size("card_same_z_a", {150.0f, 100.0f})
            .register_layer_size("card_same_z_b", {150.0f, 100.0f}).register_layer_size("card_intersect_a", {150.0f, 100.0f})
            .register_layer_size("card_intersect_b", {150.0f, 100.0f})
            .require_depth_order({"card_1", "card_2", "card_3", "card_4", "card_5"});
        return camera_test_orchestrator(ctx, s, shot, "CameraDepthSortingStressTest", {}, {0, 45, 90});
    });
}

Composition camera_subpixel_jitter_validation_test() {
    return composition({.name = "CameraSubpixelJitterValidationTest", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_yaw.set(static_cast<float>(ctx.frame()) * 0.01f);
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f}).require_visible("calibration_card", 0.0f);
        return camera_test_orchestrator(ctx, s, shot, "CameraSubpixelJitterValidationTest", {}, {0, 45, 90, 119});
    });
}

Composition camera_multi_target_bounding_box_fit_test() {
    return composition({.name = "CameraMultiTargetBoundingBoxFitTest", .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);
        s.layer("card_a", [ctx](LayerBuilder& l) { l.enable_3d().position({ctx.progress() * 250.0f, 0.0f, 0.0f}); l.rounded_rect("r_a", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.9f}}); });
        s.layer("card_b", [ctx](LayerBuilder& l) { l.enable_3d().position({-ctx.progress() * 250.0f, 0.0f, 0.0f}); l.rounded_rect("r_b", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.99f, 0.44f, 0.82f, 0.9f}}); });
        s.layer("card_c", [](LayerBuilder& l) { l.enable_3d().position({0.0f, 100.0f, 0.0f}); l.rounded_rect("r_c", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.2f, 0.9f, 0.2f, 0.9f}}); });
        CameraShotProfile shot;
        shot.rig = camera_rig_presets::orbit_reveal("camera_target");
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f})
            .register_layer_size("card_a", {200.0f, 150.0f}).register_layer_size("card_b", {200.0f, 150.0f})
            .register_layer_size("card_c", {200.0f, 150.0f})
            .require_inside_safe_area("card_a", 0.08f).require_inside_safe_area("card_b", 0.08f)
            .require_inside_safe_area("card_c", 0.08f).require_visible("card_a", 0.95f)
            .require_visible("card_b", 0.95f).require_visible("card_c", 0.95f);
        shot.auto_fit = true;
        return camera_test_orchestrator(ctx, s, shot, "CameraMultiTargetBoundingBoxFitTest", {"card_a", "card_b", "card_c"}, {0, 45, 90});
    });
}

} // namespace chronon3d::content::two_point_five_d
