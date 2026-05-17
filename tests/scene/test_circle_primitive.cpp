#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

using namespace chronon3d;

TEST_CASE("Circle primitive rendering") {
    SoftwareRenderer renderer;
    CompositionSpec spec{.width = 100, .height = 100};

    SUBCASE("Basic circle") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.circle("circle", {.radius=20.0f, .color=Color::blue(), .pos={50, 50, 0}});
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        
        // Center should be blue
        CHECK(fb->get_pixel(50, 50).b > 0.9f);
        
        // Point near edge should be blue (dist = 19.5 < 20)
        CHECK(fb->get_pixel(69, 50).b > 0.9f);
        CHECK(fb->get_pixel(50, 31).b > 0.9f);
        
        // Point on/outside edge should be black (dist >= 20.5 > 20)
        CHECK(fb->get_pixel(70, 50).b == 0.0f);
        CHECK(fb->get_pixel(50, 29).b == 0.0f);
    }

    SUBCASE("Partial clipping") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            // Circle centered on the left edge
            s.circle("clipped-circle", {.radius=20.0f, .color=Color::white(), .pos={0, 50, 0}});
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        
        CHECK(fb->get_pixel(0, 50).r > 0.9f);
        CHECK(fb->get_pixel(10, 50).r > 0.9f);
        CHECK(fb->get_pixel(19, 50).r > 0.9f);
        CHECK(fb->get_pixel(20, 50).r == 0.0f);
    }
}
