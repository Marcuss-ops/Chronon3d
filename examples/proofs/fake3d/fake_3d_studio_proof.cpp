#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/scene_builder.hpp>
#include <cmath>

using namespace chronon3d;

/**
 * Fake3DStudioProof
 *
 * Combines all Blocco 1-4 features into one scene:
 *   - Grid floor (GridPlane)
 *   - Orange thick panel (FakeBox3D)
 *   - "TILT" fake-extruded text in front
 *   - Red and blue cubes (FakeBox3D)
 *   - Contact shadows under each box
 *   - Camera orbit with elevation
 *   - Global bloom adjustment layer
 *
 * Frame 0: camera frontal
 * Frame 60: camera 180° orbit (other side)
 * Frame 119: camera ~360° (back to start)
 */
static Composition Fake3DStudioProof() {
    return composition({
        .name     = "Fake3DStudioProof",
        .width    = 1280,
        .height   = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const f32 t = ctx.effective_frame() / 120.0f;

        // Background
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size = {4000, 4000}, .color = Color{0.05f, 0.05f, 0.09f, 1.0f}});
        });

        // Camera: elevated orbit, always looking at origin
        s.null_layer("cam_rig", [&](LayerBuilder& l) {
            l.position({0, 0, 0}).rotate({0, t * 360.0f, 0});
        });
        s.enable_camera_2_5d()
         .camera_position({0, 480, -1080})
         .camera_zoom(1000.0f)
         .camera_parent("cam_rig")
         .camera_look_at({0, 0, 0});

        // Grid floor
        s.grid_plane_layer("floor", {
            .pos     = {0, -210, 0},
            .axis    = PlaneAxis::XZ,
            .extent  = 2400,
            .spacing = 240,
            .color   = Color{0.30f, 0.45f, 0.62f, 0.28f}
        });

        // --- Red cube (left) + contact shadow ---
        s.contact_shadow_layer("shadow_red", {
            .pos     = {-370, -210, 80},
            .size    = {220, 95},
            .blur    = 26.0f,
            .opacity = 0.55f
        });
        s.fake_box3d_layer("cube_red", {
            .pos   = {-370, 0, 0},
            .size  = {160, 160},
            .depth = 160,
            .color = Color{0.78f, 0.17f, 0.12f, 1.0f}
        });

        // --- Blue cube (right) + contact shadow ---
        s.contact_shadow_layer("shadow_blue", {
            .pos     = {370, -210, -80},
            .size    = {220, 95},
            .blur    = 26.0f,
            .opacity = 0.55f
        });
        s.fake_box3d_layer("cube_blue", {
            .pos   = {370, 0, 0},
            .size  = {160, 160},
            .depth = 160,
            .color = Color{0.14f, 0.38f, 0.84f, 1.0f}
        });

        // --- Orange panel (center) + contact shadow ---
        s.contact_shadow_layer("shadow_panel", {
            .pos     = {0, -210, 40},
            .size    = {640, 130},
            .blur    = 34.0f,
            .opacity = 0.48f
        });
        s.fake_box3d_layer("panel", {
            .pos   = {0, 0, 0},
            .size  = {560, 260},
            .depth = 85,
            .color = Color{0.92f, 0.43f, 0.11f, 1.0f}
        });

        // TILT fake-extruded text: 2D layer, positioned above the 3D panel in screen space.
        // In the legacy render path, 2D layers render before 3D geometry, so placing the
        // text at a screen Y above the panel (y≈180 vs panel's projected ~290-430 range)
        // keeps it clearly visible as a title above the scene.
        s.fake_extruded_text_layer("tilt", {
            .text        = "TILT",
            .pos         = {640, 180, 0},  // screen-space: above the panel
            .font_size   = 100,
            .depth       = 14,
            .extrude_dir = {1.6f, 1.6f},
            .front_color = Color{0.96f, 0.94f, 0.90f, 1.0f},
            .side_color  = Color{0.55f, 0.48f, 0.40f, 1.0f},
            .align       = TextAlign::Center
        });

        // Global bloom applied last — captures text + all 3D geometry
        s.adjustment_layer("bloom", [](LayerBuilder& l) {
            l.bloom_medium();
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Fake3DStudioProof", Fake3DStudioProof)
