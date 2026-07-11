#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/model/camera/camera_shot_profile.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include "camera_test_orchestrator.hpp"

namespace chronon3d::content::two_point_five_d {

// ── CameraFloorGridComparisonTest ─────────────────────────────────────────
// Shows TWO grid planes side by side to compare Y convention:
//   - Left grid at Y=+250 (floor — below camera, should appear BELOW horizon)
//   - Right grid at Y=-250 (ceiling — above camera, should appear ABOVE horizon)
// Camera is front-facing at (0,0,-1000), card at center as reference.
Composition camera_floor_grid_comparison_test() {
    return composition({.name = "CameraFloorGridComparisonTest", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Dark background
        s.layer("bg_fill", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.rect("bg", {.size = {1920.0f, 1080.0f}, .color = {0.04f, 0.04f, 0.06f, 1.0f}});
        });

        // Camera target at origin
        s.null_layer("camera_target", [](NullBuilder& n) {
            n.position({0.0f, 0.0f, 0.0f});
        });

        // ── Left grid: Y=+250 (FLOOR — correct Y-down convention) ──
        s.layer("grid_floor", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({-450.0f, 250.0f, 0.0f});
            l.grid_plane("grid", {
                .pos = {0.0f, 0.0f, 0.0f},
                .axis = PlaneAxis::XZ,
                .extent = 1200.0f,
                .spacing = 80.0f,
                .color = {0.15f, 0.70f, 0.30f, 0.35f},  // green tint = floor
                .fade_distance = 1800.0f,
                .fade_min_alpha = 0.02f
            });
            l.text("lbl", TextSpec{.content = {.value = "FLOOR (Y=+250)"}, .placement = {TextPlacementKind::Absolute, {0.0f, -280.0f}}, .font = {.font_size = 14.0f}, .layout = {.align = TextAlign::Center}, .appearance = {.color = {0.2f, 0.8f, 0.3f, 0.7f}}});
        });

        // ── Right grid: Y=-250 (CEILING — wrong for floor, Y-up style) ──
        s.layer("grid_ceiling", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({450.0f, -250.0f, 0.0f});
            l.grid_plane("grid", {
                .pos = {0.0f, 0.0f, 0.0f},
                .axis = PlaneAxis::XZ,
                .extent = 1200.0f,
                .spacing = 80.0f,
                .color = {0.70f, 0.30f, 0.15f, 0.35f},  // red tint = ceiling
                .fade_distance = 1800.0f,
                .fade_min_alpha = 0.02f
            });
            l.text("lbl", TextSpec{.content = {.value = "CEILING (Y=-250)"}, .placement = {TextPlacementKind::Absolute, {0.0f, -280.0f}}, .font = {.font_size = 14.0f}, .layout = {.align = TextAlign::Center}, .appearance = {.color = {0.8f, 0.3f, 0.2f, 0.7f}}});
        });

        // ── Center divider + card reference ────────────────────────
        s.layer("divider", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.line("div_line", {
                .from = {0.0f, -400.0f, 0.0f},
                .to = {0.0f, 400.0f, 0.0f},
                .thickness = 1.5f,
                .color = {0.5f, 0.5f, 0.5f, 0.4f}
            });
        });

        s.layer("center_card", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("body", {
                .size = {200.0f, 140.0f},
                .radius = 8.0f,
                .color = {0.08f, 0.10f, 0.18f, 1.0f},
                .stroke = {.enabled = true, .color = {0.0f, 0.85f, 1.0f, 0.5f}, .width = 2.0f}
            });
            l.text("center_lbl", TextSpec{.content = {.value = "CENTER"}, .placement = {TextPlacementKind::Absolute, {0.0f, -4.0f}}, .font = {.font_size = 16.0f}, .layout = {.align = TextAlign::Center}, .appearance = {.color = {0.8f, 0.85f, 1.0f, 0.85f}}});
        });

        // ── Y labels ──────────────────────────────────────────────
        s.layer("y_axis_label", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("y_down_lbl", TextSpec{.content = {.value = "Y-DOWN: +Y = ↓ (floor), -Y = ↑ (ceiling)"}, .placement = {TextPlacementKind::Absolute, {0.0f, 480.0f}}, .font = {.font_size = 14.0f}, .layout = {.align = TextAlign::Center}, .appearance = {.color = {0.5f, 0.5f, 0.5f, 0.6f}}});
        });

        // Camera: front-facing
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
            .register_layer_size("center_card", {200.0f, 140.0f})
            .require_visible("center_card", 0.60f);

        return camera_test_orchestrator(ctx, s, shot, "CameraFloorGridComparisonTest",
            {"center_card"}, {0});
    });
}

} // namespace chronon3d::content::two_point_five_d
