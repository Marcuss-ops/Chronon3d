#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/software_renderer.hpp>

using namespace chronon3d;

TEST_CASE("Alpha blending logic") {
    SoftwareRenderer renderer;
    CompositionSpec spec{.width = 100, .height = 100};

    SUBCASE("Basic transparency over black") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("bg", {50, 50, -1}, Color::black(), {100, 100});
            // 50% Red over black should result in (0.5, 0, 0)
            s.rect("box", {50, 50, 0}, Color(1, 0, 0, 0.5f), {100, 100});
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        Color c = fb->get_pixel(50, 50);
        
        CHECK(c.r == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(c.g == 0.0f);
        CHECK(c.b == 0.0f);
        CHECK(c.a == 1.0f);
    }

    SUBCASE("Overlapping semi-transparent shapes") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("bg", {50, 50, -1}, Color::black(), {100, 100});
            
            // Red 50%
            s.rect("red", {40, 50, 0}, Color(1, 0, 0, 0.5f), {40, 40});
            // Green 50% on top of red
            s.rect("green", {60, 50, 0}, Color(0, 1, 0, 0.5f), {40, 40});
            
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        
        // Pixel at center (50, 50) should have both red and green influence
        // Background was black.
        // First draw: Red (1,0,0,0.5) over Black (0,0,0,1) -> (0.5, 0, 0, 1.0)
        // Second draw: Green (0,1,0,0.5) over (0.5, 0, 0, 1.0)
        // Normal Blend: src.a * src + (1 - src.a) * dst
        // 0.5 * (0,1,0) + (1 - 0.5) * (0.5, 0, 0) = (0, 0.5, 0) + (0.25, 0, 0) = (0.25, 0.5, 0)
        
        Color c = fb->get_pixel(50, 50);
        CHECK(c.r == doctest::Approx(0.25f).epsilon(0.01));
        CHECK(c.g == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(c.b == 0.0f);
    }
}
