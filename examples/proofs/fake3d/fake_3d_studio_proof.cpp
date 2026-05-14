#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
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

        // Background: large enough to survive any SSAA factor
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size  = {99999.0f, 99999.0f},
                             .color = Materials::studio_background()});
        });

        // Camera: slightly elevated orbit, starts at 35° so the text extrusion side
        // is clearly visible from the opening frame.
        s.null_layer("cam_rig", [&](LayerBuilder& l) {
            l.position({0, 0, 0}).rotate({0, 35.0f + t * 360.0f, 0});
        });
        s.enable_camera_2_5d()
         .camera_position({0, 380, -1100})
         .camera_zoom(1020.0f)
         .camera_parent("cam_rig")
         .camera_look_at({0, 0, 0});

        // Grid floor with depth fade
        s.grid_plane_layer("floor", {
            .pos           = {0, -210, 0},
            .axis          = PlaneAxis::XZ,
            .extent        = 3000,
            .spacing       = 180,
            .color         = Materials::dark_floor_grid(),
            .fade_distance = 2200.0f,
            .fade_min_alpha = 0.0f
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

        // CHRONON — mesh extrusion (glyph outlines → side quads, camera-correct shading)
        // extrude_dir.x pushes right (visible at 25° orbit), extrude_z goes into scene
        s.fake_extruded_text_layer("tilt", {
            .text           = "CHRONON",
            .pos            = {0, 240, -80},
            .font_size      = 76,
            .depth          = 28,
            .extrude_dir    = {0.8f, 0.0f},
            .extrude_z_step = 3.0f,            // depth_z=84 → visible rim ~48px at 35°
            .front_color    = Color{0.98f, 0.95f, 0.88f, 1.0f},
            .side_color     = Color{0.48f, 0.40f, 0.28f, 1.0f},
            .side_fade      = 0.02f,
            .align          = TextAlign::Center,
            .highlight_opacity = 0.12f
        });

        // Global bloom: Soft but present
        s.adjustment_layer("bloom", [](LayerBuilder& l) {
            l.bloom_soft();
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Fake3DStudioProof", Fake3DStudioProof)

