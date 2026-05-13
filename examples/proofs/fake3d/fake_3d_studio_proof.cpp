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

        // Background: large enough to survive any SSAA factor
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size  = {99999.0f, 99999.0f},
                             .color = Materials::studio_background()});
        });

        // Camera: elevated orbit, starts at 25° for visible box sides
        s.null_layer("cam_rig", [&](LayerBuilder& l) {
            l.position({0, 0, 0}).rotate({0, 25.0f + t * 360.0f, 0});
        });
        s.enable_camera_2_5d()
         .camera_position({0, 480, -1100})
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

        // CHRONON — flat 2D-in-3D. Clean premium title, no fake-extrusion artifacts.
        s.layer("tilt", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 240, -120})
             .text("t", {
                 .content = "CHRONON",
                 .style   = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .size  = 76,
                             .color = Color{0.98f, 0.95f, 0.88f, 1.0f},
                             .align = TextAlign::Center}
             })
             .drop_shadow({0, 8}, Color{0, 0, 0, 0.55f}, 14.0f)
             .glow(12.0f, 0.20f, Color{1.0f, 0.88f, 0.55f, 1.0f});
        });

        // Global bloom: Soft but present
        s.adjustment_layer("bloom", [](LayerBuilder& l) {
            l.bloom_soft();
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Fake3DStudioProof", Fake3DStudioProof)

