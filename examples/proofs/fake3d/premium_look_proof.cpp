#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/materials.hpp>

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
         .camera_position({600.0f * std::sin(t * 0.5f) + wx, -150.0f + wy, -800.0f})
         .camera_zoom(1200.0f);

        // Studio Background
        s.rect("bg", { .size = {1920, 1080}, .color = Materials::studio_background(), .pos = {640, 360, 500} });

        // Floor Grid
        s.reflective_floor_layer("floor", {
            .pos = {0, -210, 0},
            .extent = 2000,
            .spacing = 100,
            .color = Materials::dark_floor_grid()
        });

        // Glass Panel (Floating)
        s.glass_panel_layer("glass_card", 
            {0, 50, 0},
            {400, 250},
            30.0f,
            0.6f
        );

        // Animated Boxes with Reflections and Elastic/Bounce Easing
        f32 anim_t = std::clamp(t, 0.0f, 1.0f);
        f32 elastic_t = easing::apply(Easing::OutElastic, anim_t);
        
        f32 anim_t2 = std::clamp(t - 1.0f, 0.0f, 1.0f);
        f32 bounce_t  = easing::apply(Easing::OutBounce, anim_t2);

        s.fake_box3d_layer("orange_box", {
            .pos = {-200, -210 + 300 * elastic_t, 100},
            .size = {120, 120},
            .depth = 120.0f,
            .color = Materials::studio_orange(),
            .contact_shadow = true,
            .reflective = true
        });

        s.fake_box3d_layer("blue_box", {
            .pos = {200, -210 + 200 * bounce_t, -50},
            .size = {100, 100},
            .depth = 100.0f,
            .color = Materials::studio_blue(),
            .contact_shadow = true,
            .reflective = true
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("PremiumLookProof", PremiumLookProof)
