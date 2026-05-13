#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/scene_builder.hpp>
#include <chronon3d/renderer/materials.hpp>
#include <cmath>

using namespace chronon3d;

/**
 * Fake3DStudioProof v2
 *
 * Showcases the "Look v2" improvements:
 *   - Materials presets (studio_orange, studio_blue, studio_red)
 *   - Auto contact shadows on boxes
 *   - Enhanced edge shading / fake bevels
 *   - Studio-grade background and grid
 *   - Camera orbit with soft bloom
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

        // Background: Deep studio gray
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size = {4000, 4000}, .color = Materials::studio_background()});
        });

        // Camera: Elevated orbit rig
        s.null_layer("cam_rig", [&](LayerBuilder& l) {
            l.position({0, 0, 0}).rotate({0, t * 360.0f, 0});
        });
        s.enable_camera_2_5d()
         .camera_position({0, 520, -1150})
         .camera_zoom(1050.0f)
         .camera_parent("cam_rig")
         .camera_look_at({0, 0, 0});

        // Grid floor: subtle and professional
        s.grid_plane_layer("floor", {
            .pos     = {0, -210, 0},
            .axis    = PlaneAxis::XZ,
            .extent  = 2800,
            .spacing = 200,
            .color   = Materials::dark_floor_grid()
        });

        // --- Red cube (left) ---
        s.fake_box3d_layer("cube_red", {
            .pos   = {-420, 0, 0},
            .size  = {160, 160},
            .depth = 160,
            .color = Materials::studio_red(),
            .contact_shadow = true
        });

        // --- Blue cube (right) ---
        s.fake_box3d_layer("cube_blue", {
            .pos   = {420, 0, 0},
            .size  = {160, 160},
            .depth = 160,
            .color = Materials::studio_blue(),
            .contact_shadow = true
        });

        // --- Orange panel (center) ---
        s.fake_box3d_layer("panel", {
            .pos   = {0, 0, 0},
            .size  = {580, 240},
            .depth = 90,
            .color = Materials::studio_orange(),
            .contact_shadow = true
        });

        // TILT text: Premium look
        s.fake_extruded_text_layer("tilt", {
            .text        = "CHRONON",
            .pos         = {640, 160, 0},
            .font_size   = 90,
            .depth       = 12,
            .extrude_dir = {1.5f, 1.5f},
            .front_color = Color{0.98f, 0.98f, 0.98f, 1.0f},
            .side_color  = Color{0.45f, 0.45f, 0.45f, 1.0f},
            .align       = TextAlign::Center
        });

        // Global bloom: Soft but present
        s.adjustment_layer("bloom", [](LayerBuilder& l) {
            l.bloom_soft();
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Fake3DStudioProof", Fake3DStudioProof)

