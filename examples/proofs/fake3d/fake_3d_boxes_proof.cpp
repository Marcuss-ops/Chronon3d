#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/scene_builder.hpp>
#include <cmath>

using namespace chronon3d;

/**
 * FakeBox3DProof
 *
 * Verifies FakeBox3D face projection, shading, and depth sorting.
 * Camera orbits above the scene showing front + top + side faces.
 * Three boxes: orange panel (center), red cube (left), blue cube (right).
 * Grid plane acts as the floor.
 */
static Composition FakeBox3DProof() {
    return composition({
        .name     = "FakeBox3DProof",
        .width    = 1280,
        .height   = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const f32 t = ctx.effective_frame() / 120.0f;

        // Background
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size = {4000, 4000}, .color = Color{0.06f, 0.06f, 0.10f, 1.0f}});
        });

        // Camera: elevated orbit so top faces are visible
        s.null_layer("cam_rig", [&](LayerBuilder& l) {
            l.position({0, 0, 0})
             .rotate({0, t * 360.0f, 0});
        });

        s.enable_camera_2_5d()
         .camera_position({0, 500, -1100})  // 500 above, 1100 behind rig
         .camera_zoom(1000.0f)
         .camera_parent("cam_rig")
         .camera_look_at({0, 0, 0});

        // Grid floor (XZ plane below the boxes)
        s.grid_plane_layer("floor", {
            .pos     = {0, -200, 0},
            .axis    = PlaneAxis::XZ,
            .extent  = 2200,
            .spacing = 220,
            .color   = Color{0.35f, 0.50f, 0.65f, 0.30f}
        });

        // Orange panel — center
        s.fake_box3d_layer("panel", {
            .pos   = {0, 0, 0},
            .size  = {560, 280},
            .depth = 90,
            .color = Color{0.90f, 0.42f, 0.12f, 1.0f}
        });

        // Red cube — left/front
        s.fake_box3d_layer("cube_red", {
            .pos   = {-380, 0, 100},
            .size  = {170, 170},
            .depth = 170,
            .color = Color{0.80f, 0.18f, 0.12f, 1.0f}
        });

        // Blue cube — right/back
        s.fake_box3d_layer("cube_blue", {
            .pos   = {380, 0, -100},
            .size  = {170, 170},
            .depth = 170,
            .color = Color{0.15f, 0.40f, 0.85f, 1.0f}
        });

        return s.build();
    });
}

/**
 * GridPlaneProof
 *
 * Verifies correct XZ grid plane projection with the 2.5D camera.
 * Camera is positioned high and tilted down so perspective convergence is visible.
 */
static Composition GridPlaneProof() {
    return composition({
        .name     = "GridPlaneProof",
        .width    = 1280,
        .height   = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const f32 t = ctx.effective_frame() / 120.0f;

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size = {4000, 4000}, .color = Color{0.05f, 0.06f, 0.09f, 1.0f}});
        });

        s.null_layer("cam_rig", [&](LayerBuilder& l) {
            l.position({0, 0, 0})
             .rotate({0, t * 360.0f, 0});
        });

        s.enable_camera_2_5d()
         .camera_position({0, 700, -900})
         .camera_zoom(1000.0f)
         .camera_parent("cam_rig")
         .camera_look_at({0, 0, 0});

        // Large grid: 4 wide rows of 220px spacing
        s.grid_plane_layer("floor", {
            .pos     = {0, -150, 0},
            .axis    = PlaneAxis::XZ,
            .extent  = 2600,
            .spacing = 220,
            .color   = Color{0.40f, 0.55f, 0.70f, 0.35f}
        });

        // Reference box at center to verify occlusion
        s.fake_box3d_layer("ref_box", {
            .pos   = {0, 0, 0},
            .size  = {280, 280},
            .depth = 280,
            .color = Color{0.70f, 0.70f, 0.72f, 1.0f}
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("FakeBox3DProof", FakeBox3DProof)
CHRONON_REGISTER_COMPOSITION("GridPlaneProof", GridPlaneProof)
