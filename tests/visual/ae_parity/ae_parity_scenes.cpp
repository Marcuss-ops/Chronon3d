// ==============================================================================
// tests/visual/ae_parity/ae_parity_scenes.cpp
//
// 10 AE parity camera visual scene factories.
// Each scene isolates one AE parity camera category for visual comparison
// Chronon3D ↔ After Effects.
// ==============================================================================

#include "ae_parity_scenes.hpp"

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>

#include <cmath>

using namespace chronon3d;

namespace chronon3d::test {

// ══════════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════════

namespace {

/// Build a standard grid background layer — uniform across all scenes.
void add_grid_background(SceneBuilder& s) {
    s.layer("bg", [](LayerBuilder& l) {
        l.grid_background("grid", GridBackgroundParams{
            .size = {960.0f, 540.0f},
            .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
            .grid_color = {0.28f, 0.48f, 0.98f, 0.10f},
            .spacing = 60.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true
        });
    });
}

/// Add a colored rectangular card at the given 3D position.
void add_depth_card(SceneBuilder& s, std::string name,
                    Vec3 pos, Vec2 size, Color color) {
    s.layer(name, [pos, size, color](LayerBuilder& l) {
        l.enable_3d().position(pos);
        l.rect("card", {
            .size = size,
            .color = color,
            .pos = {0.0f, 0.0f, 0.0f},
            .fill = FillStyle::solid(color)
        });
    });
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_01 — Static camera + grid background (projection baseline)
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_01_static_grid() {
    return composition(
        {.name = "ae_cam_01_static_grid", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Center cross for spatial reference.
            s.layer("cross", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rect("h", {.size = {80.0f, 6.0f}, .color = Color::white(),
                             .pos = {0.0f, 0.0f, 0.0f},
                             .fill = FillStyle::solid(Color::white())});
                l.rect("v", {.size = {6.0f, 80.0f}, .color = Color::white(),
                             .pos = {0.0f, 0.0f, 0.0f},
                             .fill = FillStyle::solid(Color::white())});
            });

            // Depth markers.
            add_depth_card(s, "far", Vec3{-300.0f, 0.0f, 300.0f},
                           Vec2{60.0f, 60.0f}, Color{0.2f, 0.4f, 1.0f, 1.0f});
            add_depth_card(s, "near", Vec3{300.0f, 0.0f, -300.0f},
                           Vec2{60.0f, 60.0f}, Color{1.0f, 0.2f, 0.2f, 1.0f});

            // Static camera, standard zoom.
            s.camera().enable()
                .position({0.0f, 0.0f, -1000.0f})
                .point_of_interest({0.0f, 0.0f, 0.0f})
                .zoom(1000.0f);

            return s.build();
        }
    );
}

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_02 — Animated zoom vs FOV
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_02_zoom_fov() {
    return composition(
        {.name = "ae_cam_02_zoom_fov", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 61},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Center subject card.
            add_depth_card(s, "subject", Vec3{0.0f, 0.0f, 0.0f},
                           Vec2{140.0f, 100.0f},
                           Color{0.99f, 0.44f, 0.82f, 1.0f});

            // Corner markers at different depths.
            add_depth_card(s, "tl", Vec3{-350.0f, -200.0f, 200.0f},
                           Vec2{40.0f, 40.0f}, Color{0.2f, 0.85f, 1.0f, 1.0f});
            add_depth_card(s, "tr", Vec3{350.0f, -200.0f, -200.0f},
                           Vec2{40.0f, 40.0f}, Color{1.0f, 0.85f, 0.2f, 1.0f});
            add_depth_card(s, "bl", Vec3{-350.0f, 200.0f, -200.0f},
                           Vec2{40.0f, 40.0f}, Color{0.2f, 1.0f, 0.4f, 1.0f});
            add_depth_card(s, "br", Vec3{350.0f, 200.0f, 200.0f},
                           Vec2{40.0f, 40.0f}, Color{0.85f, 0.2f, 1.0f, 1.0f});

            // Animated camera: zoom from 500→1500 over 60 frames.
            AnimatedCamera2_5D cam;
            cam.position.set({0.0f, 0.0f, -1000.0f});
            cam.point_of_interest.set({0.0f, 0.0f, 0.0f});
            cam.point_of_interest_enabled = true;
            cam.zoom
                .key(Frame{0}, 500.0f)
                .key(Frame{30}, 1000.0f, EasingCurve{Easing::InOutCubic})
                .key(Frame{60}, 1500.0f, EasingCurve{Easing::InOutCubic});
            s.animated_camera(cam);

            return s.build();
        }
    );
}

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_03 — Two-node camera with animated POI
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_03_two_node_poi() {
    return composition(
        {.name = "ae_cam_03_two_node_poi", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 61},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Three columns of cards at different depths.
            add_depth_card(s, "left", Vec3{-250.0f, 0.0f, 100.0f},
                           Vec2{80.0f, 120.0f}, Color{0.2f, 0.5f, 1.0f, 1.0f});
            add_depth_card(s, "center", Vec3{0.0f, 0.0f, 0.0f},
                           Vec2{80.0f, 120.0f}, Color{1.0f, 0.3f, 0.7f, 1.0f});
            add_depth_card(s, "right", Vec3{250.0f, 0.0f, -100.0f},
                           Vec2{80.0f, 120.0f}, Color{0.2f, 1.0f, 0.5f, 1.0f});

            // Null target for POI animation.
            s.null_layer("target", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
            });

            // Animated camera: position sweeps left→right, POI tracks center card.
            AnimatedCamera2_5D cam;
            cam.position
                .key(Frame{0}, Vec3{-500.0f, 0.0f, -800.0f})
                .key(Frame{30}, Vec3{0.0f, 0.0f, -1000.0f})
                .key(Frame{60}, Vec3{500.0f, 0.0f, -800.0f});
            cam.zoom.set(1000.0f);
            // POI animates across the three cards.
            cam.point_of_interest
                .key(Frame{0}, Vec3{-250.0f, 0.0f, 100.0f})
                .key(Frame{30}, Vec3{0.0f, 0.0f, 0.0f})
                .key(Frame{60}, Vec3{250.0f, 0.0f, -100.0f});
            cam.point_of_interest_enabled = true;
            s.animated_camera(cam);

            return s.build();
        }
    );
}

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_04 — Camera parented to null layer
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_04_parent_null() {
    return composition(
        {.name = "ae_cam_04_parent_null", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 61},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Subject card at origin.
            add_depth_card(s, "subject", Vec3{0.0f, 0.0f, 0.0f},
                           Vec2{120.0f, 120.0f},
                           Color{0.99f, 0.44f, 0.82f, 1.0f});

            // A null (rig) layer that moves — the camera parents to it.
            // In AE, parenting makes the child's world transform = parent * child.
            // Here we animate the camera directly with the same trajectory the
            // null layer follows, demonstrating the parent-equivalent path.
            //
            // NOTE: Full parent_name propagation through the compiled
            // CameraDescriptor path is verified in FASE 3D-E unit tests
            // (compiled_orbit_with_parent). Visual rendering through
            // SceneBuilder uses AnimatedCamera2_5D directly.
            s.null_layer("camera_rig", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
            });

            // Camera following a path that would be set by a parent null.
            AnimatedCamera2_5D cam;
            cam.position
                .key(Frame{0}, Vec3{0.0f, 0.0f, -1000.0f})
                .key(Frame{30}, Vec3{300.0f, -100.0f, -800.0f})
                .key(Frame{60}, Vec3{-300.0f, 100.0f, -1200.0f});
            cam.zoom.set(1000.0f);
            cam.point_of_interest.set({0.0f, 0.0f, 0.0f});
            cam.point_of_interest_enabled = true;
            s.animated_camera(cam);

            return s.build();
        }
    );
}

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_05 — Orbit motion with yaw sweep
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_05_orbit() {
    return composition(
        {.name = "ae_cam_05_orbit", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 61},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Subject card with accent elements around it.
            add_depth_card(s, "subject", Vec3{0.0f, 0.0f, 0.0f},
                           Vec2{200.0f, 130.0f},
                           Color{0.99f, 0.44f, 0.82f, 1.0f});

            // Orbit ring markers.
            s.layer("ring_l", [](LayerBuilder& l) {
                l.enable_3d().position({-300.0f, 0.0f, 0.0f});
                l.circle("dot", {.radius = 16.0f, .color = Color{0.2f, 0.7f, 1.0f, 1.0f},
                                 .pos = {0.0f, 0.0f, 0.0f},
                                 .fill = FillStyle::solid(Color{0.2f, 0.7f, 1.0f, 1.0f})});
            });
            s.layer("ring_r", [](LayerBuilder& l) {
                l.enable_3d().position({300.0f, 0.0f, 0.0f});
                l.circle("dot", {.radius = 16.0f, .color = Color{1.0f, 0.4f, 0.2f, 1.0f},
                                 .pos = {0.0f, 0.0f, 0.0f},
                                 .fill = FillStyle::solid(Color{1.0f, 0.4f, 0.2f, 1.0f})});
            });

            // Orbit camera: yaw sweeps -60° → +60°.
            const f32 r = 1000.0f;
            const f32 start_angle = glm::radians(-60.0f);
            const f32 end_angle = glm::radians(60.0f);
            constexpr int kSamples = 5;
            AnimatedCamera2_5D cam;
            for (int i = 0; i <= kSamples; ++i) {
                const f32 t = static_cast<f32>(i) / static_cast<f32>(kSamples);
                const f32 angle = start_angle + (end_angle - start_angle) * t;
                const Frame f = Frame{static_cast<i32>(std::round(t * 60.0f))};
                cam.position.key(f, Vec3{
                    r * std::sin(angle),
                    0.0f,
                    -r * std::cos(angle)
                });
            }
            cam.zoom.set(1000.0f);
            cam.point_of_interest.set({0.0f, 0.0f, 0.0f});
            cam.point_of_interest_enabled = true;
            s.animated_camera(cam);

            return s.build();
        }
    );
}

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_06 — Dolly zoom (Hitchcock effect)
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_06_dolly_zoom() {
    return composition(
        {.name = "ae_cam_06_dolly_zoom", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 61},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Depth layers: background, subject, foreground.
            add_depth_card(s, "bg_card", Vec3{0.0f, 0.0f, 600.0f},
                           Vec2{800.0f, 400.0f},
                           Color{0.15f, 0.3f, 0.8f, 0.6f});
            add_depth_card(s, "subject", Vec3{0.0f, 0.0f, 0.0f},
                           Vec2{160.0f, 120.0f},
                           Color{0.99f, 0.44f, 0.82f, 1.0f});
            add_depth_card(s, "fg_card", Vec3{0.0f, 0.0f, -400.0f},
                           Vec2{300.0f, 200.0f},
                           Color{0.9f, 0.2f, 0.2f, 0.4f});

            // Dolly zoom: camera moves backward while zooming in.
            AnimatedCamera2_5D cam;
            cam.position
                .key(Frame{0}, Vec3{0.0f, 0.0f, -500.0f})
                .key(Frame{60}, Vec3{0.0f, 0.0f, -2000.0f});
            cam.zoom
                .key(Frame{0}, 500.0f)
                .key(Frame{60}, 2000.0f);
            cam.point_of_interest.set({0.0f, 0.0f, 0.0f});
            cam.point_of_interest_enabled = true;
            s.animated_camera(cam);

            return s.build();
        }
    );
}

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_07 — GateFit modes comparison
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_07_gatefit() {
    return composition(
        {.name = "ae_cam_07_gatefit", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Cards placed at edges to visualise viewport distortion under
            // different GateFit modes. GateFit is selected on the camera
            // render settings at render time; this scene provides the 3D
            // geometry. The test renders this scene with different
            // GateFit modes and compares the resulting viewport mapping.
            add_depth_card(s, "center", Vec3{0.0f, 0.0f, 0.0f},
                           Vec2{100.0f, 100.0f},
                           Color{0.99f, 0.44f, 0.82f, 1.0f});
            add_depth_card(s, "top_left", Vec3{-350.0f, -200.0f, 0.0f},
                           Vec2{60.0f, 60.0f}, Color{0.2f, 0.8f, 1.0f, 1.0f});
            add_depth_card(s, "top_right", Vec3{350.0f, -200.0f, 0.0f},
                           Vec2{60.0f, 60.0f}, Color{1.0f, 0.8f, 0.2f, 1.0f});
            add_depth_card(s, "bot_left", Vec3{-350.0f, 200.0f, 0.0f},
                           Vec2{60.0f, 60.0f}, Color{0.2f, 1.0f, 0.5f, 1.0f});
            add_depth_card(s, "bot_right", Vec3{350.0f, 200.0f, 0.0f},
                           Vec2{60.0f, 60.0f}, Color{0.8f, 0.2f, 1.0f, 1.0f});

            // Wide-angle camera — with different GateFit modes the
            // corner markers will appear at different positions.
            // GateFit is verified in the compiled projection contract
            // (FASE 3, golden_projection_test.cpp) and here as a visual
            // regression lock.
            s.camera().enable()
                .position({0.0f, 0.0f, -1000.0f})
                .point_of_interest({0.0f, 0.0f, 0.0f})
                .zoom(600.0f);  // wide angle — corner markers span edges

            return s.build();
        }
    );
}

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_08 — Depth of field with animated focus distance
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_08_dof() {
    return composition(
        {.name = "ae_cam_08_dof", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 121},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Three cards at different depths.
            add_depth_card(s, "near_card", Vec3{0.0f, 0.0f, -400.0f},
                           Vec2{160.0f, 120.0f},
                           Color{1.0f, 0.2f, 0.2f, 1.0f});
            add_depth_card(s, "mid_card", Vec3{0.0f, 0.0f, 0.0f},
                           Vec2{160.0f, 120.0f},
                           Color{0.99f, 0.44f, 0.82f, 1.0f});
            add_depth_card(s, "far_card", Vec3{0.0f, 0.0f, 400.0f},
                           Vec2{160.0f, 120.0f},
                           Color{0.2f, 0.5f, 1.0f, 1.0f});

            // Camera with DOF: focus distance animates near→mid→far.
            AnimatedCamera2_5D cam;
            cam.position.set({0.0f, 0.0f, -1000.0f});
            cam.zoom.set(1000.0f);
            cam.point_of_interest.set({0.0f, 0.0f, 0.0f});
            cam.point_of_interest_enabled = true;

            // Enable DOF via AnimatedCamera2_5D native fields.
            cam.dof_enabled = true;
            cam.aperture.set(0.025f);
            cam.max_blur.set(24.0f);
            // Animate focus_z: near card (frame 0) → mid card (frame 60) → far card (frame 120).
            // Legacy model: blur = |layer_world_z - focus_z| * aperture.  focus_z must
            // equal the card's world-space Z for zero blur at the focal point.
            cam.focus_z
                .key(Frame{0}, -400.0f)    // near card is at z=-400, focus on it
                .key(Frame{60}, 0.0f)      // mid card at z=0
                .key(Frame{120}, 400.0f);  // far card at z=400
            s.animated_camera(cam);

            return s.build();
        }
    );
}

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_09 — Motion blur with fast camera pan
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_09_motion_blur() {
    return composition(
        {.name = "ae_cam_09_motion_blur", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 31},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Static cards spread across the scene.
            add_depth_card(s, "card_1", Vec3{-400.0f, -100.0f, 100.0f},
                           Vec2{60.0f, 80.0f}, Color{0.2f, 0.6f, 1.0f, 1.0f});
            add_depth_card(s, "card_2", Vec3{-200.0f, 50.0f, -50.0f},
                           Vec2{70.0f, 70.0f}, Color{0.99f, 0.44f, 0.82f, 1.0f});
            add_depth_card(s, "card_3", Vec3{0.0f, 0.0f, 0.0f},
                           Vec2{100.0f, 100.0f}, Color{1.0f, 0.6f, 0.2f, 1.0f});
            add_depth_card(s, "card_4", Vec3{200.0f, -50.0f, 50.0f},
                           Vec2{70.0f, 70.0f}, Color{0.2f, 1.0f, 0.5f, 1.0f});
            add_depth_card(s, "card_5", Vec3{400.0f, 100.0f, -100.0f},
                           Vec2{60.0f, 80.0f}, Color{0.7f, 0.2f, 1.0f, 1.0f});

            // Fast panning camera.
            // NOTE: Motion blur is configured via RenderSettings at render time,
            // not on AnimatedCamera2_5D. The PR8 motion blur unit tests
            // (test_camera_motion_blur.cpp) cover the full motion blur pipeline.
            // This scene provides the fast-camera-movement geometry baseline.
            AnimatedCamera2_5D cam;
            cam.position
                .key(Frame{0}, Vec3{-800.0f, 0.0f, -800.0f})
                .key(Frame{15}, Vec3{0.0f, 0.0f, -1000.0f})
                .key(Frame{30}, Vec3{800.0f, 0.0f, -800.0f});
            cam.zoom.set(1000.0f);
            cam.point_of_interest
                .key(Frame{0}, Vec3{-400.0f, 0.0f, 0.0f})
                .key(Frame{15}, Vec3{0.0f, 0.0f, 0.0f})
                .key(Frame{30}, Vec3{400.0f, 0.0f, 0.0f});
            cam.point_of_interest_enabled = true;

            s.animated_camera(cam);

            return s.build();
        }
    );
}

// ══════════════════════════════════════════════════════════════════════════════
// AE_CAM_10 — Near plane clipping
// ══════════════════════════════════════════════════════════════════════════════

Composition make_ae_cam_10_near_clip() {
    return composition(
        {.name = "ae_cam_10_near_clip", .width = kAeParityWidth, .height = kAeParityHeight,
         .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_grid_background(s);

            // Card that crosses the near plane (camera at z=-1000, near z≈-999).
            // It's at z=-999.5 and rotated so part of it is behind the camera.
            s.layer("cutting_card", [](LayerBuilder& l) {
                l.enable_3d()
                 .position({0.0f, 0.0f, -999.5f})
                 .rotate({0.0f, 82.0f, 0.0f});
                l.rect("card", {
                    .size = {600.0f, 400.0f},
                    .color = Color{0.9f, 0.9f, 0.9f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.9f, 0.9f, 0.9f, 1.0f})
                });
            });

            // Reference cards behind the near plane to verify they still render.
            add_depth_card(s, "ref_left", Vec3{-200.0f, 0.0f, 100.0f},
                           Vec2{50.0f, 50.0f}, Color{0.2f, 0.8f, 1.0f, 1.0f});
            add_depth_card(s, "ref_right", Vec3{200.0f, 0.0f, 100.0f},
                           Vec2{50.0f, 50.0f}, Color{1.0f, 0.4f, 0.3f, 1.0f});

            // Static camera looking forward.
            s.camera().enable()
                .position({0.0f, 0.0f, -1000.0f})
                .point_of_interest({0.0f, 0.0f, 0.0f})
                .zoom(1000.0f);

            return s.build();
        }
    );
}

} // namespace chronon3d::test
