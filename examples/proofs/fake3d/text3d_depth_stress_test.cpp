#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/scene_builder.hpp>
#include <chronon3d/core/frame.hpp>

using namespace chronon3d;

static Composition Text3DDepthStressTest() {
    return composition({
        .name     = "Text3DDepthStressTest",
        .width    = 1280,
        .height   = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        
        // Studio Camera
        s.camera_2_5d({
            .enabled = true,
            .position = {0, 100, -800},
            .point_of_interest = {0, 0, 0},
            .fov_deg = 45.0f
        });
        
        s.layer("Background", [](LayerBuilder& l) {
            l.rect("bg", { .size = {1280, 720}, .color = Color(0.05f, 0.05f, 0.05f) });
        });
        
        s.layer("StressScene", [&ctx](LayerBuilder& l) {
            l.enable_3d();
            
            // Slowly rotate to see the intersection from different angles
            f32 angle = (f32)ctx.frame * 1.5f;
            l.rotate({0, 35.0f + angle, 0});

            // 1. Text "CHRONON"
            // At Z=0, depth 100
            FakeExtrudedTextParams tp;
            tp.text = "CHRONON";
            tp.pos = {-400, 0, 0};
            tp.font_size = 140.0f;
            tp.depth = 120;
            tp.front_color = Color(0.95f, 0.85f, 0.3f); 
            tp.side_color = Color(0.6f, 0.5f, 0.2f);
            tp.bevel_size = 3.0f;
            l.fake_extruded_text("text", tp);
            
            // 2. Intersecting Box
            // Centered at Z=60 (halfway through text depth), 
            // so it should poke through the text.
            FakeBox3DParams bp;
            bp.pos = {-50, 0, 60}; 
            bp.size = {250, 250};
            bp.depth = 250; 
            bp.color = Color(0.1f, 0.4f, 0.9f, 0.8f); // semi-transparent to help debug
            l.fake_box3d("cube", bp);
        });
        
        return s.build();
    });
}

static Composition Text3DWindingTest() {
    return composition({
        .name     = "Text3DWindingTest",
        .width    = 1280,
        .height   = 720,
        .duration = 60
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({ .enabled = true, .position = {0, 0, -1000}, .fov_deg = 30.0f });
        s.layer("Background", [](LayerBuilder& l) { l.rect("bg", { .size = {1280, 720}, .color = Color(0.1f, 0.1f, 0.1f) }); });
        
        s.layer("WindingScene", [](LayerBuilder& l) {
            l.enable_3d();
            // Killer glyphs for triangulation: O, 8, A, @
            FakeExtrudedTextParams tp;
            tp.text = "O 8 A @";
            tp.pos = {-500, 0, 0};
            tp.font_size = 250.0f;
            tp.depth = 150;
            tp.front_color = Color(0.2f, 0.8f, 0.4f);
            tp.side_color = Color(0.1f, 0.4f, 0.2f);
            l.fake_extruded_text("glyphs", tp);
            
            // Slight rotation to see sides and caps
            l.rotate({15.0f, 20.0f, 0});
        });
        return s.build();
    });
}

static Composition Text3DOrientationTest() {
    return composition({
        .name     = "Text3DOrientationTest",
        .width    = 1280,
        .height   = 720,
        .duration = 60
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Camera orbiting 180 degrees
        f32 orbit = (f32)ctx.frame / 60.0f * 180.0f;
        s.camera_2_5d({ .enabled = true, .position = {0, 0, -1000}, .fov_deg = 30.0f });
        s.layer("Background", [](LayerBuilder& l) { l.rect("bg", { .size = {1280, 720}, .color = Color(0.1f, 0.05f, 0.1f) }); });
        
        s.layer("OrientationScene", [orbit](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({0, orbit, 0});

            FakeExtrudedTextParams tp;
            tp.text = "CHRONON";
            tp.pos = {-350, 0, 0};
            tp.font_size = 120.0f;
            tp.depth = 100;
            tp.front_color = Color::white();
            tp.side_color = Color(0.4f, 0.4f, 0.4f);
            l.fake_extruded_text("text", tp);
            
            // Reference Arrow pointing to +X (Right from front)
            FakeBox3DParams bp;
            bp.pos = {0, -150, 50};
            bp.size = {300, 40};
            bp.depth = 40;
            bp.color = Color::red();
            l.fake_box3d("arrow_shaft", bp);
            
            bp.pos = {150, -150, 50};
            bp.size = {60, 100};
            l.fake_box3d("arrow_head", bp);
        });
        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Text3DDepthStressTest", Text3DDepthStressTest)
CHRONON_REGISTER_COMPOSITION("Text3DWindingTest", Text3DWindingTest)
CHRONON_REGISTER_COMPOSITION("Text3DOrientationTest", Text3DOrientationTest)
