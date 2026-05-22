#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/composition_registry.hpp>

using namespace chronon3d;

TEST_CASE("Precomposition rendering via RenderGraph") {
    CompositionRegistry registry;

    // 1. Child Composition (a simple green rect)
    registry.add("ChildComp", []() {
        return composition({
            .name = "ChildComp",
            .width = 100,
            .height = 100,
            .duration = 60
        }, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("green-box", {
                .size = {50, 50},
                .color = Color{0, 1, 0, 1},
                .pos = {0, 0, 0}
            });
            return s.build();
        });
    });

    // 2. Main Composition (includes ChildComp)
    Composition main_comp = composition({
        .name = "MainComp",
        .width = 200,
        .height = 200,
        .duration = 60
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Add precomp at center (0,0,0)
        s.precomp_layer("nested", "ChildComp", [](LayerBuilder& l) {
            l.position({0, 0, 0});
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    renderer.set_composition_registry(&registry);
    
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(main_comp, 0);

    // Pixel at (100, 100) should be green.
    auto p = fb->get_pixel(100, 100);
    CHECK(p.g > 0.5f);
    CHECK(p.r < 0.1f);
}

TEST_CASE("Precomposition respects nested time") {
    CompositionRegistry registry;

    // Child Comp: rect moves from left to right based on frame
    registry.add("AnimatedChild", []() {
        return composition({
            .name = "AnimatedChild",
            .width = 100,
            .height = 100,
            .duration = 100
        }, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            float x = (float)ctx.frame; // moves 1 pixel per frame
            s.rect("moving-box", {
                .size = {10, 10},
                .color = Color::white(),
                .pos = {x, 0, 0}
            });
            return s.build();
        });
    });

    // Main Comp: starts precomp at frame 10
    Composition main_comp = composition({
        .name = "MainComp",
        .width = 100,
        .height = 100,
        .duration = 100
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.precomp_layer("nested", "AnimatedChild", [](LayerBuilder& l) {
            l.from(10).duration(50);
            l.position({0, 0, 0});
        });
        return s.build();
    });

    SoftwareRenderer renderer;
    renderer.set_composition_registry(&registry);
    
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    // At Main Frame 15, Child should be at its Frame 5.
    // So rect should be at x=5 (relative to center).
    // Absolute pixel: 50 + 5 = 55.
    auto fb = renderer.render_frame(main_comp, 15);
    auto p = fb->get_pixel(55, 50);
    CHECK(p.r > 0.5f);
    
    // At Main Frame 5, Child is not active yet.
    auto fb2 = renderer.render_frame(main_comp, 5);
    auto p2 = fb2->get_pixel(55, 50);
    CHECK(p2.a == 0.0f);
}
