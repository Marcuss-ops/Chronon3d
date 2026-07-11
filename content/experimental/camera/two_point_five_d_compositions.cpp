#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/math/color.hpp>
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include "camera_test_orchestrator.hpp"
#include "camera_advanced_tests.hpp"
#endif

namespace chronon3d::content::two_point_five_d {

namespace {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

} // namespace

Composition parallax_simple() {
    return composition({.name = "ParallaxSimple", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        const f32 cam_x = -p * 400.0f;

        s.camera().set(camera_motion::dolly(p, {.from_z = -900, .to_z = -720, .zoom = 800}));

        s.layer("far_bg", [&](auto& l) { 
            l.enable_3d().position({cam_x * 0.2f, 0.0f, 300.0f});
            l.rect("far", {.size = {W * 2, H}, .color = {0.02f, 0.03f, 0.08f, 1}});
        });

        s.layer("mid_bg", [&](auto& l) {
            l.enable_3d().position({cam_x * 0.5f, 0.0f, 100.0f});
            l.rect("mid", {.size = {W * 1.5f, H}, .color = {0.04f, 0.06f, 0.12f, 1}});
        });

        for (int i = 0; i < 5; ++i) {
            s.layer("mid_elem_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({i * 500.0f - 1000.0f + cam_x * 0.5f, static_cast<f32>(i % 3 - 1) * 200.0f, 100.0f});
                l.circle("c", {.radius = 20.0f + i * 10.0f, .color = {0.25f, 0.52f, 1, 0.15f}});
            });
        }

        s.layer("fg", [&](auto& l) {
            l.enable_3d().position({cam_x * 1.0f, 0.0f, -200.0f});
            l.rect("fg_rect", {.size = {W, 300}, .color = {0.06f, 0.08f, 0.14f, 1}, .pos = {0, 400, 0}});
        });

        for (int i = 0; i < 3; ++i) {
            s.layer("fg_box_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({i * 600.0f - 600.0f + cam_x * 1.0f, -50.0f - i * 80.0f, -200.0f});
                l.rounded_rect("box", {.size = {180, 140}, .radius = 12, .color = {0.25f, 0.52f, 1, 0.4f}});
            });
        }

        s.layer("label", [&](auto& l) {
            l.opacity(0.7f).pin_to(Anchor::BottomLeft, 40.0f);
            l.text("txt", TextSpec{.content = {.value = "Parallax Demo  |  Far(0.2x)  Mid(0.5x)  FG(1.0x)"}, .placement = {TextPlacementKind::Absolute, {0.0f, 0.0f}}, .font = {.font_family = "Inter", .font_weight = 800, .font_size = 22.0f}, .layout = {.box = {W*0.5f, 30}, .align = TextAlign::Left, .line_height = 1.2f, .tracking = 1.5f}, .appearance = {.color = Color{0.6f, 0.7f, 0.9f, 1}}});
        });

        return s.build();
    });
}

Composition depth_scene() {
    return composition({.name = "DepthScene", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        const f32 cam_z = camera_motion::lerp(-800.0f, -500.0f, camera_motion::smoothstep(p));

        s.camera().set(camera_motion::push_in_tilt(p, {.from_z = -800, .to_z = -500, .from_tilt = -4, .to_tilt = 4, .zoom = 800}));
        s.ambient_light({1, 1, 1, 1}, 0.2f);
        s.directional_light({-0.5f, 1, -0.5f}, {1, 1, 1, 1}, 0.8f);

        auto depth_l = [&](const char* name, f32 z, auto draw_func) {
            s.layer(name, [&](auto& l) { l.enable_3d().position({0, 0, z}); draw_func(l); });
        };

        depth_l("far_layer", 200, [](auto& l) { l.rect("far", {.size = {1600, 900}, .color = {0.05f, 0.08f, 0.20f, 1}}); });
        depth_l("mid_layer", 0, [](auto& l) {
            l.rounded_rect("mid", {.size = {800, 500}, .radius = 24, .color = {0.12f, 0.15f, 0.25f, 1}});
            l.rect("inner", {.size = {400, 200}, .color = {0.25f, 0.52f, 1, 0.12f}});
        });
        depth_l("near_layer", -150, [](auto& l) {
            l.position({0, -100, -150});
            l.rounded_rect("near", {.size = {300, 80}, .radius = 16, .color = {0.25f, 0.52f, 1, 0.6f}});
        });

        for (int i = 0; i < 4; ++i) {
            const f32 angle = i * 1.5708f;
            s.layer("pillar_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({std::cos(angle) * 500.0f, 0, std::sin(angle) * 100.0f}).casts_shadows(true).accepts_shadows(true);
                l.fake_box3d("box", {.size = {40, 300}, .depth = 40, .color = {0.2f, 0.3f, 0.5f, 1}, .contact_shadow = true, .floor_y = -250});
            });
        }

        s.layer("floor", [](auto& l) {
            l.enable_3d().position({0, -250, 0}).accepts_shadows(true);
            l.rect("floor", {.size = {2000, 2000}, .color = {0.03f, 0.04f, 0.06f, 1}});
        });

        s.layer("info", [&](auto& l) {
            l.pin_to(Anchor::BottomRight, 40.0f);
            l.text("txt", TextSpec{.content = {.value = "Depth: " + std::to_string(static_cast<i32>(cam_z))}, .placement = {TextPlacementKind::Absolute, {0.0f, 0.0f}}, .font = {.font_family = "Inter", .font_weight = 800, .font_size = 20.0f}, .layout = {.box = {200, 30}, .align = TextAlign::Right, .line_height = 1.2f, .tracking = 0.0f}, .appearance = {.color = Color{0.6f, 0.7f, 0.9f, 0.5f}}});
        });

        return s.build();
    });
}

Composition card_flip() {
    return composition({.name = "CardFlip", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        const f32 angle_y = p * 360.0f;
        const f32 scale_x = std::cos(angle_y * 0.0174533f);

        s.camera().set(camera_motion::orbit_small(p, 800.0f));
        s.layer("bg", [](auto& l) { l.fill({0.008f, 0.012f, 0.025f, 1}); });

        s.layer("card", [&](auto& l) {
            l.enable_3d().rotate({0, angle_y, 0}).casts_shadows(true).accepts_shadows(true);
            if (scale_x > 0) {
                l.rounded_rect("front", {.size = {400, 560}, .radius = 20, .color = {0.12f, 0.15f, 0.25f, 1}});
                l.rect("header", {.size = {360, 80}, .color = {0.25f, 0.52f, 1, 0.3f}, .pos = {0, -200, 0.1f}});
                l.text("label", TextSpec{.content = {.value = "FLIP"}, .placement = {TextPlacementKind::Absolute, {0, 0}}, .font = {.font_family = "Inter", .font_weight = 800, .font_size = 56.0f}, .layout = {.box = {320, 80}, .align = TextAlign::Center, .line_height = 1.2f, .tracking = 8.0f}, .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}}});
            } else {
                l.rounded_rect("back", {.size = {400, 560}, .radius = 20, .color = {0.08f, 0.10f, 0.18f, 1}});
                l.text("label", TextSpec{.content = {.value = "2.5D"}, .placement = {TextPlacementKind::Absolute, {0, 0}}, .font = {.font_family = "Inter", .font_weight = 800, .font_size = 56.0f}, .layout = {.box = {320, 80}, .align = TextAlign::Center, .line_height = 1.2f, .tracking = 8.0f}, .appearance = {.color = Color{0.4f, 0.6f, 0.9f, 1}}});
            }
        });

        s.layer("floor_shadow", [&](auto& l) {
            l.enable_3d().position({0, -350, 0});
            l.rect("shadow", {.size = {400 * (std::abs(scale_x) * 0.8f + 0.2f), 60}, .color = {0, 0, 0, 0.2f * (1 - std::abs(scale_x) * 0.3f)}});
        });

        return s.build();
    });
}

Composition dof_showcase() {
    return composition({.name = "DofShowcase", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();

        // Slow dolly towards the scene; focus stays on the mid layer at z=0
        const f32 cam_z = -1000.0f + p * 200.0f;
        s.camera().set({
            .enabled = true,
            .position = {0.0f, 0.0f, cam_z},
            .zoom = 1000.0f,
            .dof = DepthOfFieldSettings{
                .enabled  = true,
                .focus_z  = 0.0f,     // sharp focus at z=0 (mid layer)
                .aperture = 0.015f,   // moderate blur per unit of z-distance
                .max_blur  = 18.0f    // clamp for extreme depths
            }
        });

        // ── Far background layer (z=400) — heavily blurred ──────────────
        s.layer("far_bg", [&](auto& l) {
            l.enable_3d().position({0, 0, 400});
            l.rect("bg", {.size = {W * 1.5f, H * 1.2f}, .color = {0.03f, 0.04f, 0.10f, 1}});
        });

        // Far decorative circles — blurred bokeh-like shapes
        for (int i = 0; i < 6; ++i) {
            const f32 angle = i * 1.047f; // 60° apart
            s.layer("far_dot_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({
                    std::cos(angle) * 500.0f,
                    std::sin(angle) * 300.0f,
                    350.0f
                });
                const f32 r = 30.0f + static_cast<f32>(i) * 15.0f;
                l.circle("dot", {.radius = r, .color = {0.20f, 0.35f, 0.80f, 0.25f}});
            });
        }

        // ── Mid layer (z=0) — in focus, the subject ────────────────────
        s.layer("subject_card", [&](auto& l) {
            l.enable_3d().position({0, 0, 0});
            l.rounded_rect("card", {.size = {500, 320}, .radius = 24,
                .color = {0.12f, 0.15f, 0.25f, 1}});
        });

        s.layer("subject_title", [&](auto& l) {
            l.enable_3d().position({0, 20, 1});
            l.text("title", TextSpec{.content = {.value = "DEPTH OF FIELD"}, .placement = {TextPlacementKind::Absolute, {0.0f, 0.0f}}, .font = {.font_family = "Inter", .font_weight = 800, .font_size = 44.0f}, .layout = {.box = {440, 60}, .align = TextAlign::Center, .line_height = 1.2f, .tracking = 6.0f}, .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}}});
        });

        s.layer("subject_sub", [&](auto& l) {
            l.enable_3d().position({0, -40, 1});
            l.text("sub", TextSpec{.content = {.value = "Subject sharp \u2022 Depth blur"}, .placement = {TextPlacementKind::Absolute, {0.0f, 0.0f}}, .font = {.font_family = "Inter", .font_weight = 600, .font_size = 20.0f}, .layout = {.box = {400, 40}, .align = TextAlign::Center, .line_height = 1.2f, .tracking = 1.0f}, .appearance = {.color = Color{0.6f, 0.7f, 0.9f, 0.8f}}});
        });

        // ── Near foreground layer (z=-250) — slightly blurred ───────────
        s.layer("near_fg", [&](auto& l) {
            l.enable_3d().position({0, -200, -250});
            l.rect("bar", {.size = {W * 1.5f, 120}, .color = {0.06f, 0.08f, 0.16f, 0.85f}});
        });

        // Foreground decorative elements — blurred because they're closer than focus
        for (int i = 0; i < 3; ++i) {
            s.layer("near_box_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({
                    static_cast<f32>(i - 1) * 450.0f,
                    -180.0f,
                    -300.0f
                });
                l.rounded_rect("box", {.size = {140, 60}, .radius = 12,
                    .color = {0.25f, 0.52f, 1.0f, 0.3f}});
            });
        }

        // ── HUD label ──────────────────────────────────────────────────
        s.layer("hud", [&](auto& l) {
            l.opacity(0.6f).pin_to(Anchor::BottomLeft, 40.0f);
            l.text("info", TextSpec{.content = {.value = "DOF Showcase  |  focus_z=0  aperture=0.015  max_blur=18"}, .placement = {TextPlacementKind::Absolute, {0.0f, 0.0f}}, .font = {.font_family = "Inter", .font_weight = 800, .font_size = 18.0f}, .layout = {.box = {W * 0.55f, 30}, .align = TextAlign::Left, .line_height = 1.2f, .tracking = 1.5f}, .appearance = {.color = Color{0.5f, 0.6f, 0.85f, 1}}});
        });

        return s.build();
    });
}

// ── Per-domain registration ──────────────────────────────────────────────────
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
// Forward-declare camera test factories from companion files
Composition camera_orbit_target_lock_test();
Composition camera_dolly_perspective_scale_test();
Composition camera_parent_null_rig_test();
Composition camera_roll_pan_tilt_grid_test();
Composition camera_safe_framing_aspect_ratio_16_9();
Composition camera_safe_framing_aspect_ratio_1_1();
Composition camera_safe_framing_aspect_ratio_9_16();
Composition camera_safe_framing_aspect_ratio_4_5();
Composition camera_frustum_culling_precision_test();
Composition camera_kinematic_jerk_interpolation_test();
Composition camera_depth_sorting_stress_test();
Composition camera_subpixel_jitter_validation_test();
Composition camera_multi_target_bounding_box_fit_test();
Composition camera_depth_perspective_scale_diagnostic_test();
Composition camera_coordinate_contract_test();
Composition camera_binding_anchor_test();
Composition camera_front_baseline_test();
Composition camera_yaw_positive_test();
Composition camera_yaw_negative_test();
#endif

void register_2d5_compositions(CompositionRegistry& registry) {
    // Product compositions
    registry.add("ParallaxSimple", [](const CompositionProps&) { return parallax_simple(); });
    registry.add("DepthScene", [](const CompositionProps&) { return depth_scene(); });
    registry.add("CardFlip", [](const CompositionProps&) { return card_flip(); });
    registry.add("DofShowcase", [](const CompositionProps&) { return dof_showcase(); });
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    // Diagnostic camera test compositions
    registry.add("CameraOrbitTargetLockTest", [](const CompositionProps&) { return camera_orbit_target_lock_test(); });
    registry.add("CameraDollyPerspectiveScaleTest", [](const CompositionProps&) { return camera_dolly_perspective_scale_test(); });
    registry.add("CameraParentNullRigTest", [](const CompositionProps&) { return camera_parent_null_rig_test(); });
    registry.add("CameraRollPanTiltGridTest", [](const CompositionProps&) { return camera_roll_pan_tilt_grid_test(); });
    registry.add("CameraSafeFramingAspectRatioTest_16_9", [](const CompositionProps&) { return camera_safe_framing_aspect_ratio_16_9(); });
    registry.add("CameraSafeFramingAspectRatioTest_1_1", [](const CompositionProps&) { return camera_safe_framing_aspect_ratio_1_1(); });
    registry.add("CameraSafeFramingAspectRatioTest_9_16", [](const CompositionProps&) { return camera_safe_framing_aspect_ratio_9_16(); });
    registry.add("CameraSafeFramingAspectRatioTest_4_5", [](const CompositionProps&) { return camera_safe_framing_aspect_ratio_4_5(); });
    registry.add("CameraFrustumCullingPrecisionTest", [](const CompositionProps&) { return camera_frustum_culling_precision_test(); });
    registry.add("CameraKinematicJerkAndInterpolationTest", [](const CompositionProps&) { return camera_kinematic_jerk_interpolation_test(); });
    registry.add("CameraDepthSortingStressTest", [](const CompositionProps&) { return camera_depth_sorting_stress_test(); });
    registry.add("CameraSubpixelJitterValidationTest", [](const CompositionProps&) { return camera_subpixel_jitter_validation_test(); });
    registry.add("CameraMultiTargetBoundingBoxFitTest", [](const CompositionProps&) { return camera_multi_target_bounding_box_fit_test(); });
    registry.add("CameraDepthPerspectiveScaleDiagnosticTest", [](const CompositionProps&) { return camera_depth_perspective_scale_diagnostic_test(); });
    registry.add("CameraCoordinateContractTest", [](const CompositionProps&) { return camera_coordinate_contract_test(); });
    registry.add("CameraBindingAnchorTest", [](const CompositionProps&) { return camera_binding_anchor_test(); });
    registry.add("CameraFrontBaselineTest", [](const CompositionProps&) { return camera_front_baseline_test(); });
    registry.add("CameraYawPositiveTest", [](const CompositionProps&) { return camera_yaw_positive_test(); });
    registry.add("CameraYawNegativeTest", [](const CompositionProps&) { return camera_yaw_negative_test(); });
#endif
}

} // namespace chronon3d::content::two_point_five_d
