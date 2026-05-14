#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;

/**
 * ContactShadowProof
 *
 * Three boxes side-by-side with varying contact shadow settings:
 * left = no shadow, center = soft shadow (blur 28), right = strong (blur 18, higher opacity).
 * Camera slightly elevated so top faces are visible; static at frame 0.
 */
static Composition ContactShadowProof() {
    return composition({
        .name     = "ContactShadowProof",
        .width    = 1280,
        .height   = 720,
        .duration = 60
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const f32 t = ctx.effective_frame() / 60.0f;

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size = {4000, 4000}, .color = Color{0.06f, 0.06f, 0.10f, 1.0f}});
        });

        // Slow orbit so the 3D effect is visible
        s.null_layer("cam_rig", [&](LayerBuilder& l) {
            l.position({0, 0, 0}).rotate({0, t * 30.0f - 15.0f, 0});
        });
        s.enable_camera_2_5d()
         .camera_position({0, 450, -1050})
         .camera_zoom(1000.0f)
         .camera_parent("cam_rig")
         .camera_look_at({0, 0, 0});

        // Floor grid
        s.grid_plane_layer("floor", {
            .pos = {0, -180, 0}, .axis = PlaneAxis::XZ,
            .extent = 2000, .spacing = 200,
            .color  = Color{0.35f, 0.50f, 0.65f, 0.25f}
        });

        // --- Left: no shadow ---
        s.fake_box3d_layer("box_l", {
            .pos   = {-400, 0, 0},
            .size  = {180, 240},
            .depth = 80,
            .color = Color{0.70f, 0.70f, 0.72f, 1.0f}
        });

        // --- Center: soft contact shadow ---
        s.contact_shadow_layer("shadow_c", {
            .pos     = {0, -180, 30},
            .size    = {240, 100},
            .blur    = 28.0f,
            .opacity = 0.50f,
        });
        s.fake_box3d_layer("box_c", {
            .pos   = {0, 0, 0},
            .size  = {180, 240},
            .depth = 80,
            .color = Color{0.90f, 0.42f, 0.12f, 1.0f}
        });

        // --- Right: stronger shadow ---
        s.contact_shadow_layer("shadow_r", {
            .pos     = {400, -180, 30},
            .size    = {220, 90},
            .blur    = 16.0f,
            .opacity = 0.70f,
        });
        s.fake_box3d_layer("box_r", {
            .pos   = {400, 0, 0},
            .size  = {180, 240},
            .depth = 80,
            .color = Color{0.20f, 0.40f, 0.82f, 1.0f}
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ContactShadowProof", ContactShadowProof)
