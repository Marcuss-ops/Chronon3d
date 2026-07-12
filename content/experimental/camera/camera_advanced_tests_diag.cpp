#include "camera_advanced_tests.hpp"
#include "camera_test_orchestrator.hpp"
#include "camera_calibration_scene.hpp"
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/camera/camera_shot_profile.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>

namespace chronon3d::content::two_point_five_d {

// ═════════════════════════════════════════════════════════════════════════════
// Diagnostic / contract tests — depth perspective, coordinate contract,
// binding anchor, front baseline, yaw positive/negative.
// ═════════════════════════════════════════════════════════════════════════════

Composition camera_depth_perspective_scale_diagnostic_test() {
    return composition({.name = "CameraDepthPerspectiveScaleDiagnosticTest", .width = 1920, .height = 1080, .duration = 91}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s);
        s.layer("depth_far", [](LayerBuilder& l) { l.cache_static().enable_3d().position({-100.0f, 0.0f, 400.0f}); l.rounded_rect("bg", {.size = {300.0f, 180.0f}, .radius = 14.0f, .color = Color{0.06f, 0.09f, 0.22f, 0.85f}, .stroke = {.enabled = true, .color = Color{0.12f, 0.22f, 0.55f, 0.40f}, .width = 1.5f}}); l.text("label", TextSpec{.content = {.value = "FAR (Z=+400)"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 18.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.50f, 0.65f, 0.90f, 0.85f}},}); l.text("desc", TextSpec{.content = {.value = "Smallest on screen"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 11.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.40f, 0.50f, 0.75f, 0.55f}},}); });
        s.layer("depth_mid_far", [](LayerBuilder& l) { l.cache_static().enable_3d().position({350.0f, -30.0f, 200.0f}); l.rounded_rect("bg", {.size = {250.0f, 150.0f}, .radius = 12.0f, .color = Color{0.10f, 0.15f, 0.38f, 0.88f}, .stroke = {.enabled = true, .color = Color{0.18f, 0.38f, 0.82f, 0.50f}, .width = 2.0f}}); l.text("label", TextSpec{.content = {.value = "MID-FAR (Z=+200)"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 16.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.55f, 0.72f, 1.0f, 0.90f}},}); l.text("desc", TextSpec{.content = {.value = "Behind center"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 10.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.50f, 0.60f, 0.85f, 0.55f}},}); });
        s.layer("depth_center", [](LayerBuilder& l) { l.cache_static().enable_3d().position({-300.0f, 20.0f, 0.0f}); l.rounded_rect("bg", {.size = {250.0f, 160.0f}, .radius = 12.0f, .color = Color{0.18f, 0.30f, 0.72f, 0.92f}, .stroke = {.enabled = true, .color = Color{0.30f, 0.55f, 1.0f, 0.60f}, .width = 2.0f}}); l.text("label", TextSpec{.content = {.value = "CENTER (Z=0)"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 18.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.80f, 0.88f, 1.0f, 1.0f}},}); l.text("desc", TextSpec{.content = {.value = "Reference"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 11.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.65f, 0.75f, 1.0f, 0.65f}},}); });
        s.layer("depth_near", [](LayerBuilder& l) { l.cache_static().enable_3d().position({200.0f, 60.0f, -200.0f}); l.rounded_rect("bg", {.size = {240.0f, 140.0f}, .radius = 10.0f, .color = Color{0.04f, 0.28f, 0.52f, 0.92f}, .stroke = {.enabled = true, .color = Color{0.08f, 0.65f, 0.92f, 0.70f}, .width = 2.0f}}); l.text("label", TextSpec{.content = {.value = "NEAR (Z=-200)"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 18.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.55f, 0.92f, 1.0f, 1.0f}},}); l.text("desc", TextSpec{.content = {.value = "Larger on screen"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 11.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.45f, 0.82f, 1.0f, 0.70f}},}); });
        s.layer("depth_foreground", [](LayerBuilder& l) { l.cache_static().enable_3d().position({-200.0f, -40.0f, -400.0f}); l.rounded_rect("bg", {.size = {220.0f, 130.0f}, .radius = 10.0f, .color = Color{0.02f, 0.42f, 0.75f, 1.0f}, .stroke = {.enabled = true, .color = Color{0.25f, 0.90f, 1.0f, 0.85f}, .width = 3.0f}}); l.text("label", TextSpec{.content = {.value = "FG (Z=-400)"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 18.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.75f, 1.0f, 1.0f, 1.0f}},}); l.text("desc", TextSpec{.content = {.value = "Closest — largest"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 11.0f},.layout = {.align = TextAlign::Center},.appearance = {.color = Color{0.65f, 0.95f, 1.0f, 0.75f}},}); });
        struct DepthCard { const char* name; float px; float py; float z; const char* z_label; };
        DepthCard cards[] = {{"depth_foreground", -200.0f, -40.0f, -400.0f, "Z=-400"}, {"depth_near", 200.0f, 60.0f, -200.0f, "Z=-200"}, {"depth_center", -300.0f, 20.0f, 0.0f, "Z=0"}, {"depth_mid_far", 350.0f, -30.0f, 200.0f, "Z=+200"}, {"depth_far", -100.0f, 0.0f, 400.0f, "Z=+400"}};
        for (const auto& c : cards) {
            float alpha = 0.20f + 0.15f * (1.0f - std::abs(c.z) / 400.0f);
            float drop_y = 250.0f - c.py;
            s.layer(std::string("drop_") + c.name, [c, alpha, drop_y](LayerBuilder& l) {
                l.enable_3d().position({c.px, c.py, c.z});
                l.line("drop", LineParams{.from = {0.0f, 0.0f, 0.0f}, .to = {0.0f, drop_y, 0.0f}, .thickness = 1.2f, .color = Color{0.6f, 0.8f, 1.0f, alpha}});
                l.circle("base_dot", {.radius = 3.5f, .color = Color{0.6f, 0.8f, 1.0f, std::min(alpha * 1.5f, 1.0f)}, .pos = {0.0f, drop_y, 0.1f}});
                l.text("z_lbl", TextSpec{.content = {.value = std::string(c.z_label)},.placement = TextPlacement{TextPlacementKind::Absolute, {10.0f, 0.0f}},.font = {.font_size = 9.0f},.layout = {.align = TextAlign::Left},.appearance = {.color = Color{0.55f, 0.75f, 1.0f, alpha * 1.2f}},});
            });
        }
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.key(0, 1400.0f).key(22, 900.0f, EasingCurve{Easing::InOutCubic}).key(45, 500.0f, EasingCurve{Easing::InOutCubic}).key(67, 900.0f, EasingCurve{Easing::InOutCubic}).key(90, 1400.0f, EasingCurve{Easing::InOutCubic});
        shot.rig.orbit_yaw.key(0, -12.0f).key(45, 12.0f, EasingCurve{Easing::InOutSine}).key(90, -12.0f, EasingCurve{Easing::InOutSine});
        shot.rig.orbit_pitch.set(-4.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(54.4f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f}).register_layer_size("depth_far", {300.0f, 180.0f}).register_layer_size("depth_mid_far", {250.0f, 150.0f}).register_layer_size("depth_center", {250.0f, 160.0f}).register_layer_size("depth_near", {240.0f, 140.0f}).register_layer_size("depth_foreground", {220.0f, 130.0f}).require_depth_order({"depth_foreground", "depth_near", "depth_center", "depth_mid_far", "depth_far"});
        return camera_test_orchestrator(ctx, s, shot, "CameraDepthPerspectiveScaleDiagnosticTest", {"depth_far", "depth_mid_far", "depth_center", "depth_near", "depth_foreground"}, {0, 45, 90});
    });
}

Composition camera_coordinate_contract_test() {
    constexpr f32 CARD_W = 700.0f, CARD_H = 420.0f, HALF_W = CARD_W * 0.5f, HALF_H = CARD_H * 0.5f, CORNER = 48.0f;
    return composition({.name = "CameraCoordinateContractTest", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg_fill", [](LayerBuilder& l) { l.pin_to(Anchor::Center); l.rect("bg", {.size = {1920.0f, 1080.0f}, .color = {0.04f, 0.04f, 0.06f, 1.0f}}); });
        s.layer("screen_center", [](LayerBuilder& l) { l.pin_to(Anchor::Center); l.line("ch_h", {.from = {-40.0f, 0.0f, 0.0f}, .to = {40.0f, 0.0f, 0.0f}, .thickness = 1.5f, .color = {1.0f, 1.0f, 1.0f, 0.6f}}); l.line("ch_v", {.from = {0.0f, -40.0f, 0.0f}, .to = {0.0f, 40.0f, 0.0f}, .thickness = 1.5f, .color = {1.0f, 1.0f, 1.0f, 0.6f}}); l.text("lbl", TextSpec{.content = {.value = "VIEWPORT CENTER"},.placement = TextPlacement{TextPlacementKind::Absolute, {44.0f, 0.0f}},.font = {.font_size = 9.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {1.0f, 1.0f, 1.0f, 0.4f}},}); });
        s.layer("axis_x", [](LayerBuilder& l) { l.enable_3d().position({0.0f, 0.0f, 0.0f}); l.line("x_line", {.from = {0.0f, 0.0f, 0.0f}, .to = {300.0f, 0.0f, 0.0f}, .thickness = 2.5f, .color = {1.0f, 0.2f, 0.2f, 0.8f}}); l.text("x_lbl", TextSpec{.content = {.value = "X"},.placement = TextPlacement{TextPlacementKind::Absolute, {308.0f, 0.0f}},.font = {.font_size = 18.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {1.0f, 0.2f, 0.2f, 0.8f}},}); });
        s.layer("axis_y", [](LayerBuilder& l) { l.enable_3d().position({0.0f, 0.0f, 0.0f}); l.line("y_line", {.from = {0.0f, 0.0f, 0.0f}, .to = {0.0f, 250.0f, 0.0f}, .thickness = 2.5f, .color = {0.2f, 1.0f, 0.2f, 0.8f}}); l.text("y_lbl", TextSpec{.content = {.value = "Y"},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 18.0f},.layout = {.anchor = TextAnchor::BottomCenter, .align = TextAlign::Center},.appearance = {.color = {0.2f, 1.0f, 0.2f, 0.8f}},}); });
        s.null_layer("camera_target", [](NullBuilder& n) { n.position({0.0f, 0.0f, 0.0f}); });
        s.layer("target_marker", [](LayerBuilder& l) { l.enable_3d().position({0.0f, 0.0f, 0.0f}); l.circle("target_dot", {.radius = 7.0f, .color = {1.0f, 0.9f, 0.1f, 1.0f}, .pos = {0.0f, 0.0f, 0.3f}}); l.circle("target_ring", {.radius = 14.0f, .color = {0.0f, 0.0f, 0.0f, 0.0f}, .pos = {0.0f, 0.0f, 0.25f}, .stroke = {.enabled = true, .color = {1.0f, 0.9f, 0.1f, 0.6f}, .width = 2.5f}}); l.text("lbl", TextSpec{.content = {.value = "TARGET"},.placement = TextPlacement{TextPlacementKind::Absolute, {18.0f, 0.0f}},.font = {.font_size = 11.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {1.0f, 0.9f, 0.1f, 0.7f}},}); });
        s.layer("origin_marker", [](LayerBuilder& l) { l.enable_3d().position({0.0f, 0.0f, 0.0f}); l.circle("origin_dot", {.radius = 4.0f, .color = {0.5f, 0.5f, 0.5f, 0.7f}, .pos = {0.0f, 0.0f, 0.2f}}); l.circle("origin_ring", {.radius = 10.0f, .color = {0.0f, 0.0f, 0.0f, 0.0f}, .pos = {0.0f, 0.0f, 0.18f}, .stroke = {.enabled = true, .color = {0.5f, 0.5f, 0.5f, 0.4f}, .width = 1.5f}}); l.text("lbl", TextSpec{.content = {.value = "ORIGIN"},.placement = TextPlacement{TextPlacementKind::Absolute, {14.0f, 0.0f}},.font = {.font_size = 9.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {0.5f, 0.5f, 0.5f, 0.5f}},}); });
        s.layer("diagnostic_card", [](LayerBuilder& l) { l.enable_3d().position({0.0f, 0.0f, 0.0f}); l.rounded_rect("card_body", {.size = {CARD_W, CARD_H}, .radius = 6.0f, .color = {0.06f, 0.06f, 0.10f, 1.0f}, .stroke = {.enabled = true, .color = {0.7f, 0.72f, 0.8f, 0.6f}, .width = 2.0f}}); l.rect("corner_tl", {.size = {CORNER, CORNER}, .color = {1.0f, 0.0f, 0.0f, 1.0f}, .pos = {-HALF_W + 2.0f, -HALF_H + 2.0f, 0.1f}}); l.rect("corner_tr", {.size = {CORNER, CORNER}, .color = {0.0f, 1.0f, 0.0f, 1.0f}, .pos = {HALF_W - CORNER - 2.0f, -HALF_H + 2.0f, 0.1f}}); l.rect("corner_bl", {.size = {CORNER, CORNER}, .color = {0.0f, 0.0f, 1.0f, 1.0f}, .pos = {-HALF_W + 2.0f, HALF_H - CORNER - 2.0f, 0.1f}}); l.rect("corner_br", {.size = {CORNER, CORNER}, .color = {1.0f, 1.0f, 0.0f, 1.0f}, .pos = {HALF_W - CORNER - 2.0f, HALF_H - CORNER - 2.0f, 0.1f}}); l.line("center_h", {.from = {-20.0f, 0.0f, 0.2f}, .to = {20.0f, 0.0f, 0.2f}, .thickness = 2.0f, .color = {0.0f, 1.0f, 1.0f, 0.8f}}); l.line("center_v", {.from = {0.0f, -20.0f, 0.2f}, .to = {0.0f, 20.0f, 0.2f}, .thickness = 2.0f, .color = {0.0f, 1.0f, 1.0f, 0.8f}}); l.text("center_lbl", TextSpec{.content = {.value = "CENTER"},.placement = TextPlacement{TextPlacementKind::Absolute, {24.0f, 0.0f}},.font = {.font_size = 10.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {0.0f, 1.0f, 1.0f, 0.6f}},}); l.text("tl_lbl", TextSpec{.content = {.value = "TL"},.placement = TextPlacement{TextPlacementKind::Absolute, {-HALF_W + 4.0f, 0.0f}},.font = {.font_size = 10.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {1.0f, 0.3f, 0.3f, 0.7f}},}); l.text("tr_lbl", TextSpec{.content = {.value = "TR"},.placement = TextPlacement{TextPlacementKind::Absolute, {HALF_W - CORNER - 52.0f, 0.0f}},.font = {.font_size = 10.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {0.3f, 1.0f, 0.3f, 0.7f}},}); l.text("bl_lbl", TextSpec{.content = {.value = "BL"},.placement = TextPlacement{TextPlacementKind::Absolute, {-HALF_W + 38.0f, 0.0f}},.font = {.font_size = 10.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {0.3f, 0.3f, 1.0f, 0.7f}},}); l.text("br_lbl", TextSpec{.content = {.value = "BR"},.placement = TextPlacement{TextPlacementKind::Absolute, {HALF_W - CORNER - 52.0f, 0.0f}},.font = {.font_size = 10.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {1.0f, 1.0f, 0.3f, 0.7f}},}); });
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.orbit_yaw.set(0.0f); shot.rig.orbit_pitch.set(0.0f); shot.rig.roll.set(0.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("diagnostic_card", {CARD_W, CARD_H}).require_target_centered("camera_target", 1.0f).require_visible("diagnostic_card", 0.80f);
        return camera_test_orchestrator(ctx, s, shot, "CameraCoordinateContractTest", {"diagnostic_card"}, {0});
    });
}

Composition camera_binding_anchor_test() {
    constexpr f32 CARD_OFFSET_X = 80.0f, CARD_OFFSET_Y = 40.0f, CARD_W = 400.0f, CARD_H = 280.0f;
    return composition({.name = "CameraBindingAnchorTest", .width = 1920, .height = 1080, .duration = 2}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg_fill", [](LayerBuilder& l) { l.pin_to(Anchor::Center); l.rect("bg", {.size = {1920.0f, 1080.0f}, .color = {0.04f, 0.04f, 0.06f, 1.0f}}); });
        s.layer("screen_center", [](LayerBuilder& l) { l.pin_to(Anchor::Center); l.line("ch_h", {.from = {-30.0f, 0.0f, 0.0f}, .to = {30.0f, 0.0f, 0.0f}, .thickness = 1.0f, .color = {1.0f, 1.0f, 1.0f, 0.5f}}); l.line("ch_v", {.from = {0.0f, -30.0f, 0.0f}, .to = {0.0f, 30.0f, 0.0f}, .thickness = 1.0f, .color = {1.0f, 1.0f, 1.0f, 0.5f}}); });
        s.null_layer("origin_target", [](NullBuilder& n) { n.position({0.0f, 0.0f, 0.0f}); });
        s.null_layer("visual_center_target", [](NullBuilder& n) { n.position({CARD_OFFSET_X, CARD_OFFSET_Y, 0.0f}); });
        s.layer("test_card", [](LayerBuilder& l) { l.enable_3d().position({0.0f, 0.0f, 0.0f}); l.rounded_rect("card_body", {.size = {CARD_W, CARD_H}, .radius = 12.0f, .color = {0.08f, 0.10f, 0.18f, 1.0f}, .pos = {CARD_OFFSET_X, CARD_OFFSET_Y, 0.0f}, .stroke = {.enabled = true, .color = {0.0f, 0.85f, 1.0f, 0.5f}, .width = 2.0f}}); l.line("card_center_h", {.from = {CARD_OFFSET_X - 20.0f, CARD_OFFSET_Y, 0.2f}, .to = {CARD_OFFSET_X + 20.0f, CARD_OFFSET_Y, 0.2f}, .thickness = 2.5f, .color = {1.0f, 0.85f, 0.1f, 1.0f}}); l.line("card_center_v", {.from = {CARD_OFFSET_X, CARD_OFFSET_Y - 20.0f, 0.2f}, .to = {CARD_OFFSET_X, CARD_OFFSET_Y + 20.0f, 0.2f}, .thickness = 2.5f, .color = {1.0f, 0.85f, 0.1f, 1.0f}}); l.circle("pivot_dot", {.radius = 6.0f, .color = {1.0f, 0.0f, 0.0f, 1.0f}, .pos = {0.0f, 0.0f, 0.3f}}); l.line("pivot_h", {.from = {-12.0f, 0.0f, 0.3f}, .to = {12.0f, 0.0f, 0.3f}, .thickness = 1.5f, .color = {1.0f, 0.5f, 0.5f, 0.6f}}); l.line("pivot_v", {.from = {0.0f, -12.0f, 0.3f}, .to = {0.0f, 12.0f, 0.3f}, .thickness = 1.5f, .color = {1.0f, 0.5f, 0.5f, 0.6f}}); l.text("pivot_label", TextSpec{.content = {.value = "PIVOT"},.placement = TextPlacement{TextPlacementKind::Absolute, {14.0f, 0.0f}},.font = {.font_size = 11.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {1.0f, 0.5f, 0.5f, 0.7f}},}); l.text("center_label", TextSpec{.content = {.value = "CENTER"},.placement = TextPlacement{TextPlacementKind::Absolute, {CARD_OFFSET_X + 22.0f, 0.0f}},.font = {.font_size = 11.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = {1.0f, 0.85f, 0.1f, 0.8f}},}); });
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.orbit_yaw.set(0.0f); shot.rig.orbit_pitch.set(0.0f); shot.rig.roll.set(0.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.rig.target_name = (ctx.frame == 0) ? "origin_target" : "visual_center_target";
        s.layer("frame_label", [ctx](LayerBuilder& l) { std::string label = (ctx.frame == 0) ? "LOOK AT PIVOT" : "LOOK AT VISUAL CENTER"; Color label_color = (ctx.frame == 0) ? Color{1.0f, 0.4f, 0.4f, 0.9f} : Color{0.4f, 1.0f, 0.4f, 0.9f}; l.pin_to(Anchor::TopLeft, 20.0f); l.text("hud", TextSpec{.content = {.value = label},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},.font = {.font_size = 22.0f},.layout = {.anchor = TextAnchor::TopLeft, .align = TextAlign::Left},.appearance = {.color = label_color},}); });
        shot.validator.register_layer_size("test_card", {CARD_W, CARD_H}).require_visible("test_card", 0.60f);
        return camera_test_orchestrator(ctx, s, shot, "CameraBindingAnchorTest", {"test_card"}, {0, 1});
    });
}

Composition camera_front_baseline_test() {
    return composition({.name = "CameraFrontBaselineTest", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s, false);
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.orbit_yaw.set(0.0f); shot.rig.orbit_pitch.set(0.0f); shot.rig.roll.set(0.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f}).require_target_centered("camera_target", 1.0f).require_visible("calibration_card", 0.80f);
        return camera_test_orchestrator(ctx, s, shot, "CameraFrontBaselineTest", {"calibration_card"}, {0});
    });
}

Composition camera_yaw_positive_test() {
    return composition({.name = "CameraYawPositiveTest", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s, false);
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.orbit_yaw.set(30.0f);
        shot.rig.orbit_pitch.set(0.0f); shot.rig.roll.set(0.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f}).require_target_centered("camera_target", 2.0f).require_visible("calibration_card", 0.70f);
        return camera_test_orchestrator(ctx, s, shot, "CameraYawPositiveTest", {"calibration_card"}, {0});
    });
}

Composition camera_yaw_negative_test() {
    return composition({.name = "CameraYawNegativeTest", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_camera_calibration_scene(s, false);
        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.orbit_yaw.set(-30.0f);
        shot.rig.orbit_pitch.set(0.0f); shot.rig.roll.set(0.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);
        shot.validator.register_layer_size("calibration_card", {500.0f, 350.0f}).require_target_centered("camera_target", 2.0f).require_visible("calibration_card", 0.70f);
        return camera_test_orchestrator(ctx, s, shot, "CameraYawNegativeTest", {"calibration_card"}, {0});
    });
}

} // namespace chronon3d::content::two_point_five_d
