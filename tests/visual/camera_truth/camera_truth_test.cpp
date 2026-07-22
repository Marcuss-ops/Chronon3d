// ==============================================================================
// tests/visual/camera_truth/camera_truth_test.cpp
//
// CameraTruthTest — 3D camera projection truth scene.
//
// Three colored rectangles in 3D world-space at different depths,
// with a TwoNode camera that pans left/right.  Designed to prove
// that the render path actually projects world-space coordinates
// through the camera — the closest card should move the most (parallax).
// ==============================================================================

#include "camera_truth_test.hpp"

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <spdlog/spdlog.h>

using namespace chronon3d;

namespace chronon3d::test {

Composition make_camera_truth_test() {
    return composition(
        {.name = "CameraTruthTest", .width = 960, .height = 540, .duration = 61},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            const Frame f = ctx.frame();

            // ── Background grid (2D, screen-space reference) ──────────
            s.layer("bg", [](LayerBuilder& l) {
                l.grid_background("grid", GridBackgroundParams{
                    .size = {960.0f, 540.0f},
                    .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                    .grid_color = {0.28f, 0.48f, 0.98f, 0.08f},
                    .spacing = 60.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            // ── Four 3D cards at different Z depths ───────────────────
            // Dark red (behind cam) = z=-1200 — behind-camera clipping test (depth negative)
            // Red (near)       = z=0     — should move the most
            // Green (mid)      = z=500   — should move moderately
            // Blue (far)       = z=1000  — should move the least (parallax)

            s.layer("card_behind", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, -1200.0f});
                l.rect("rect", {
                    .size = {120.0f, 80.0f},
                    .color = Color{0.85f, 0.10f, 0.10f, 0.6f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.85f, 0.10f, 0.10f, 0.6f})
                });
            });

            s.layer("card_near", [](LayerBuilder& l) {
                l.enable_3d().position({-300.0f, 0.0f, 0.0f});
                l.rect("rect", {
                    .size = {100.0f, 65.0f},
                    .color = Color{1.0f, 0.15f, 0.15f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 0.15f, 0.15f, 1.0f})
                });
            });

            s.layer("card_mid", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 500.0f});
                l.rect("rect", {
                    .size = {100.0f, 65.0f},
                    .color = Color{0.15f, 0.9f, 0.20f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.15f, 0.9f, 0.20f, 1.0f})
                });
            });

            s.layer("card_far", [](LayerBuilder& l) {
                l.enable_3d().position({300.0f, 0.0f, 1000.0f});
                l.rect("rect", {
                    .size = {100.0f, 65.0f},
                    .color = Color{0.15f, 0.35f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.15f, 0.35f, 1.0f, 1.0f})
                });
            });

            // ── Crosshair at origin (3D, at z=0) ─────────────────────
            s.layer("origin_cross", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rect("h", {.size = {50.0f, 3.0f}, .color = Color::white(),
                             .pos = {0.0f, 0.0f, 0.0f},
                             .fill = FillStyle::solid(Color::white())});
                l.rect("v", {.size = {3.0f, 50.0f}, .color = Color::white(),
                             .pos = {0.0f, 0.0f, 0.0f},
                             .fill = FillStyle::solid(Color::white())});
            });

            // ── Camera: TwoNode, pans across frames 0/30/60 ───────────
            AnimatedCamera2_5D cam;
            // Camera starts at origin, moves right, then left.
            cam.position
                .key(Frame{0},  Vec3{0.0f,   0.0f, -1000.0f})
                .key(Frame{30}, Vec3{300.0f, 0.0f, -1000.0f})
                .key(Frame{60}, Vec3{-300.0f, 0.0f, -1000.0f});
            cam.zoom.set(1000.0f);
            cam.point_of_interest.set({0.0f, 0.0f, 0.0f});
            cam.point_of_interest_enabled = true;
            s.animated_camera(cam);

            // ── Diagnostic: log world→screen projection for each card ─
            // Camera at z=-1000, cards at z=0, 500, 1000.
            // Depth = cam_z - card_z = cam.position.z - card.position.z
            // perspective_scale = zoom / depth
            // screen_x = vp_w/2 + (card_x - cam_x) * perspective_scale
            // screen_y = vp_h/2 - (card_y - cam_y) * perspective_scale

            spdlog::info("[CameraTruth] frame={} cam_pos=({:.0f},{:.0f},{:.0f})",
                         static_cast<int>(f),
                         cam.position.evaluate(f).x,
                         cam.position.evaluate(f).y,
                         cam.position.evaluate(f).z);

            auto log_card = [f, &cam](const char* name, Vec3 world_pos) {
                Vec3 campos = cam.position.evaluate(f);
                float depth = world_pos.z - campos.z;  // world_z - camera_z (camera at -1000, card at 0 => depth=1000)
                float zoom = cam.zoom.evaluate(f);
                float scale = (depth != 0.0f) ? zoom / depth : 0.0f;
                float screen_x = 480.0f + (world_pos.x - campos.x) * scale;
                float screen_y = 270.0f - (world_pos.y - campos.y) * scale;
                spdlog::info("[CameraTruth] frame={} layer={:10s} "
                             "world=({:6.0f},{:6.0f},{:6.0f}) "
                             "depth={:7.0f} scale={:.3f} "
                             "screen=({:7.1f},{:7.1f}) visible={}",
                             static_cast<int>(f), name,
                             world_pos.x, world_pos.y, world_pos.z,
                             depth, scale,
                             screen_x, screen_y,
                             (depth > 0.0f) ? "true" : "false");
            };

            log_card("behind",     Vec3{0.0f, 0.0f, -1200.0f});
            log_card("near(red)",  Vec3{-300.0f, 0.0f, 0.0f});
            log_card("mid(green)", Vec3{0.0f, 0.0f, 500.0f});
            log_card("far(blue)",  Vec3{300.0f, 0.0f, 1000.0f});

            return s.build();
        }
    );
}

} // namespace chronon3d::test
