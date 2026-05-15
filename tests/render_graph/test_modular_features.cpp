#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/composition_registry.hpp>

using namespace chronon3d;

TEST_CASE("Modular Graph: TransformNode validation") {
    CompositionRegistry registry;
    
    Composition comp = composition({
        .name = "TransformTest",
        .width = 100,
        .height = 100,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // A 50x50 red rect shifted 25 pixels right and 50% opacity
        s.layer("transformed", [](LayerBuilder& l) {
            l.position({25, 0, 0});
            l.opacity(0.5f);
            l.rect("red-box", {
                .size = {50, 50},
                .color = Color{1, 0, 0, 1},
                .pos = {0, 0, 0}
            });
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);

    // Center of scene is (50, 50) in pixels.
    // Shifted by 25 right -> (75, 50) in pixels should be red.
    auto p_center = fb->get_pixel(75, 50);
    CHECK(p_center.r > 0.4f);
    CHECK(p_center.a == doctest::Approx(0.5f).epsilon(0.01f));
    
    auto p_outside = fb->get_pixel(40, 50);
    CHECK(p_outside.a == 0.0f);
}

TEST_CASE("Modular Graph: MaskNode validation") {
    Composition comp = composition({
        .name = "MaskTest",
        .width = 100,
        .height = 100,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        
        // A blue rect 100x100
        // Masked by a circle at (0,0) with radius 20
        s.layer("masked-layer", [](LayerBuilder& l) {
            l.mask_circle({.radius = 20.0f});
            l.rect("blue-bg", {
                .size = {100, 100},
                .color = Color{0, 0, 1, 1},
                .pos = {0, 0, 0}
            });
        });
        
        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);

    // Center of scene is (50, 50).
    // Circle radius is 20, so points within 20 pixels of (50, 50) should be blue.
    // Points outside should be transparent.
    
    auto p_inside = fb->get_pixel(50, 50);
    CHECK(p_inside.b > 0.5f);
    CHECK(p_inside.a > 0.5f);
    
    auto p_outside = fb->get_pixel(10, 10);
    CHECK(p_outside.a == 0.0f);
    
    auto p_edge_inside = fb->get_pixel(65, 50); // dist = 15 < 20
    CHECK(p_edge_inside.a > 0.5f);
    
    auto p_edge_outside = fb->get_pixel(75, 50); // dist = 25 > 20
    CHECK(p_edge_outside.a == 0.0f);
}

TEST_CASE("Modular Graph: Adjustment Layer validation") {
    Composition comp = composition({
        .name = "AdjustmentTest",
        .width = 100,
        .height = 100,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        
        // Bottom layer: Red rect
        s.layer("bottom", [](LayerBuilder& l) {
            l.rect("red", { .size={100, 100}, .color={1, 0, 0, 1}, .pos={0, 0, 0} });
        });
        
        // Adjustment layer: Tint (Green)
        // This should tint the red below it to some brownish color if tinting green over red.
        // Actually tint() in Chronon3d replaces or overlays color.
        s.layer("adj", [](LayerBuilder& l) {
            l.kind(LayerKind::Adjustment);
            l.tint(Color{0, 1, 0, 1}, 1.0f); 
        });
        
        // Top layer: White small rect (should NOT be tinted green)
        s.layer("top", [](LayerBuilder& l) {
            l.rect("white", { .size={20, 20}, .color={1, 1, 1, 1}, .pos={0, 0, 0} });
        });
        
        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);

    // The background (red rect) should be tinted green.
    // If amount is 1.0, it might be fully green depending on tint implementation.
    auto p_bg = fb->get_pixel(10, 10);
    CHECK(p_bg.g > 0.5f);
    CHECK(p_bg.r < 0.5f); // Red was tinted away
    
    // The top white rect should still be white.
    auto p_top = fb->get_pixel(50, 50);
    CHECK(p_top.r > 0.9f);
    CHECK(p_top.g > 0.9f);
    CHECK(p_top.b > 0.9f);
}

TEST_CASE("Modular Graph: Sampling Mode validation") {
    Composition comp = composition({
        .name = "SamplingTest",
        .width = 100,
        .height = 100,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // A single white pixel scaled 2x and shifted by 0.5
        // If shifted by 0.5, Nearest might pick it or not.
        // Bilinear should produce 0.5 opacity/color at the edges.
        s.layer("scaled", [](LayerBuilder& l) {
            l.position({0.5f, 0.0f, 0.0f});
            l.rect("pixel", { .size={1, 1}, .color={1, 1, 1, 1}, .pos={0, 0, 0} });
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);

    // Scene (0,0) is pixel (50, 50).
    // Box is at (0.5, 0) with size (1, 1).
    // Box range in scene: (0, -0.5) to (1, 0.5)
    // Box range in pixels: (50, 49.5) to (51, 50.5)
    
    // Pixel (50, 50) center is (50.5, 50.5).
    // x = 50.5 is in [50, 51].
    // y = 50.5 is NOT in [49.5, 50.5) because of half-open range hit test in Rect.
    auto p = fb->get_pixel(50, 50);
    CHECK(p.a == 0.0f); 
}

TEST_CASE("Render settings coordinate mode regression") {
    Composition comp = composition({
        .name = "CoordinateModeTest",
        .width = 100,
        .height = 100,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("box", {
            .size = {10, 10},
            .color = Color{1, 0, 0, 1},
            .pos = {0, 0, 0}
        });
        return s.build();
    });

    SoftwareRenderer top_left_renderer;
    auto top_left_fb = top_left_renderer.render_frame(comp, 0);
    CHECK(top_left_fb->get_pixel(0, 0).r > 0.9f);
    CHECK(top_left_fb->get_pixel(50, 50).r == 0.0f);

    SoftwareRenderer modular_renderer;
    RenderSettings modular_settings;
    modular_settings.use_modular_graph = true;
    modular_renderer.set_settings(modular_settings);

    auto modular_fb = modular_renderer.render_frame(comp, 0);
    CHECK(modular_fb->get_pixel(50, 50).r > 0.9f);
    CHECK(modular_fb->get_pixel(0, 0).r == 0.0f);
}
