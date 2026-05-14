#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/materials.hpp>
#include <chronon3d/presets/studio_presets.hpp>

using namespace chronon3d;

static Composition PremiumLookProof() {
    CompositionSpec spec;
    spec.name = "PremiumLookProof";
    spec.width = 1280;
    spec.height = 720;
    spec.duration = 180; // 3 seconds

    return composition(spec, [](const FrameContext& ctx) {
        SceneBuilder s;
        f32 t = static_cast<f32>(ctx.frame) / 60.0f;

        // Camera Orbit with Wiggle for "handheld" feel
        Wiggle cam_wiggle{1.5f, 2.0f, 1234};
        f32 wx = cam_wiggle.eval(t);
        f32 wy = cam_wiggle.eval(t + 10.0f);

        s.enable_camera_2_5d(true)
         .camera_position({600.0f * std::sin(t * 0.5f) + wx, 300.0f + wy, -900.0f})
         .camera_zoom(1100.0f)
         .camera_look_at({0, 0, 0});

        // Studio Background — full-screen layer, not affected by 2.5D camera
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", { .size = {4000, 4000}, .color = Materials::studio_background() });
        });

        // Floor Grid
        presets::Studio::grid_plane(s, "floor", {
            .pos = {0, -210, 0},
            .extent = 2000,
            .spacing = 100,
            .color = Materials::dark_floor_grid()
        });

        // Glass Panel (Floating) — not 3D, renders as 2D overlay
        s.layer("glass_card", [](LayerBuilder& l) {
            l.kind(LayerKind::Glass)
             .position({640, 360, 0})
             .rect("glass_base", { .size = {400, 250}, .color = Color::white() })
             .opacity(0.5f)
             .blur(20.0f);
        });

        // Animated Boxes with Reflections and Elastic/Bounce Easing
        f32 anim_t = std::clamp(t, 0.0f, 1.0f);
        f32 elastic_t = easing::apply(Ease::OutElastic, anim_t);
        
        f32 anim_t2 = std::clamp(t - 1.0f, 0.0f, 1.0f);
        f32 bounce_t  = easing::apply(Ease::OutBounce, anim_t2);

        presets::Studio::fake_box3d(s, "orange_box", {
            .pos = {-200, -60 + 200 * elastic_t, 100},
            .size = {120, 120},
            .depth = 120.0f,
            .color = Materials::studio_orange(),
            .contact_shadow = true,
            .reflective = true,
            .floor_y = -210.0f
        });

        presets::Studio::fake_box3d(s, "blue_box", {
            .pos = {200, -60 + 160 * bounce_t, -50},
            .size = {100, 100},
            .depth = 100.0f,
            .color = Materials::studio_blue(),
            .contact_shadow = true,
            .reflective = true,
            .floor_y = -210.0f
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("PremiumLookProof", PremiumLookProof)
