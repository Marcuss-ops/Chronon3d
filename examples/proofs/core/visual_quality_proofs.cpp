#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/materials.hpp>
#include <chronon3d/presets/studio_presets.hpp>
#include <algorithm>

using namespace chronon3d;

// 1. Test SSAA — quadrato rosso su nero (MASSIMO CONTRASTO)
Composition VisualTest_SSAA() {
    CompositionSpec spec;
    spec.name = "VisualTest_SSAA";
    spec.width = 400;
    spec.height = 400;

    return composition(spec, [](const FrameContext& ctx) {
        SceneBuilder s;
        // Background layer
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("black", { .size = {800, 800}, .color = Color::black() });
        });

        s.layer("square_layer", [](LayerBuilder& l) {
            l.position({200, 200, 0})  // centro del canvas 400x400
             .rotate({0, 0, 30.0f})
             .rect("sq", { .size = {200, 200}, .color = Color::from_hex("#ff0000") });
        });
        return s.build();
    });
}

// 2. Test Floor Reflection
Composition VisualTest_Reflection() {
    CompositionSpec spec;
    spec.name = "VisualTest_Reflection";
    spec.width = 800;
    spec.height = 600;

    return composition(spec, [](const FrameContext& ctx) {
        SceneBuilder s;
        s.enable_camera_2_5d(true)
         .camera_position({0, -100, -800});

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("dark", { .size = {1600, 1200}, .color = Color::from_hex("#0a0a0a") });
        });

        // Floor at Y = -120
        presets::Studio::grid_plane(s, "floor", {
            .pos = {0, -120, 0},
            .axis = PlaneAxis::XZ,
            .extent = 2000,
            .spacing = 100,
            .color = Color::from_hex("#2c3e50").with_alpha(0.6f)
        });

        presets::Studio::fake_box3d(s, "cube", {
            .pos = {0, 0, 0},
            .size = {120, 120},
            .depth = 120,
            .color = Color::from_hex("#3498db"),
            .contact_shadow = true,
            .reflective = true,
            .floor_y = -120.0f
        });

        return s.build();
    });
}

// 3. Test Glassmorphism
Composition VisualTest_Glass() {
    CompositionSpec spec;
    spec.name = "VisualTest_Glass";
    spec.width = 800;
    spec.height = 600;

    return composition(spec, [](const FrameContext& ctx) {
        SceneBuilder s;
        
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("gray", { .size = {1600, 1200}, .color = Color::from_hex("#111111") });
        });

        // Colorful markers behind glass (absolute coords for 800x600)
        s.layer("bg_elements", [](LayerBuilder& l) {
            l.circle("c1", { .radius = 120, .color = Color::from_hex("#e74c3c"), .pos = {200, 180, 0} });
            l.circle("c2", { .radius = 100, .color = Color::from_hex("#2ecc71"), .pos = {580, 200, 0} });
            l.circle("c3", { .radius = 140, .color = Color::from_hex("#f1c40f"), .pos = {400, 450, 0} });
        });

        // Glass panel centered
        presets::Studio::glass_panel(s, "glass", {400, 300, 0}, {400, 300}, 25.0f, 0.4f);

        return s.build();
    });
}

// 4. Test Easing Curves
Composition VisualTest_Easing() {
    CompositionSpec spec;
    spec.name = "VisualTest_Easing";
    spec.width = 800;
    spec.height = 600;
    spec.duration = 60; 

    return composition(spec, [](const FrameContext& ctx) {
        SceneBuilder s;
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("black", { .size = {1600, 1200}, .color = Color::from_hex("#050505") });
        });

        f32 t = std::clamp(static_cast<f32>(ctx.frame) / 60.0f, 0.0f, 1.0f);
        
        // Elastic (Blue) — travels from x=100 to x=700, bounces around y=200
        f32 elastic_v = easing::apply(Ease::OutElastic, t);
        const f32 ex = 100 + t * 600;
        const f32 bx = 100 + t * 600;
        f32 bounce_v = easing::apply(Ease::OutBounce, t);
        s.layer("markers", [&](LayerBuilder& l) {
            l.circle("elastic", { .radius = 15, .color = Color::from_hex("#3498db"), .pos = {ex, 200 - 100 * elastic_v, 0} });
            l.circle("bounce",  { .radius = 15, .color = Color::from_hex("#e67e22"), .pos = {bx, 400 - 100 * bounce_v, 0} });
        });

        return s.build();
    });
}

// 5. Test Wiggle
Composition VisualTest_Wiggle() {
    CompositionSpec spec;
    spec.name = "VisualTest_Wiggle";
    spec.width = 800;
    spec.height = 600;
    spec.duration = 120;

    return composition(spec, [](const FrameContext& ctx) {
        SceneBuilder s;
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("black", { .size = {1600, 1200}, .color = Color::from_hex("#050505") });
        });

        f32 time = static_cast<f32>(ctx.frame) / 60.0f;
        Wiggle w{1.5f, 20.0f, 1234}; 
        f32 shake_x = w.eval(time);
        f32 shake_y = w.eval(time + 10.0f);

        s.enable_camera_2_5d(true)
         .camera_position({shake_x, shake_y, -600});

        presets::Studio::fake_box3d(s, "target", { 
            .pos = {0, 0, 0}, 
            .size = {150, 150}, 
            .depth = 150,
            .color = Color::white()
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("VisualTest_SSAA", VisualTest_SSAA)
CHRONON_REGISTER_COMPOSITION("VisualTest_Reflection", VisualTest_Reflection)
CHRONON_REGISTER_COMPOSITION("VisualTest_Glass", VisualTest_Glass)
CHRONON_REGISTER_COMPOSITION("VisualTest_Easing", VisualTest_Easing)
CHRONON_REGISTER_COMPOSITION("VisualTest_Wiggle", VisualTest_Wiggle)
