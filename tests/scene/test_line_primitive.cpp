#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

using namespace chronon3d;

TEST_CASE("Line primitive rendering") {
    SoftwareRenderer renderer;
    CompositionSpec spec{.width = 100, .height = 100};

    SUBCASE("Horizontal line") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.line("h-line", {0, 50, 0}, {100, 50, 0}, Color::red());
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        CHECK(fb->get_pixel(10, 50).r > 0.9f);
        CHECK(fb->get_pixel(50, 50).r > 0.9f);
        CHECK(fb->get_pixel(90, 50).r > 0.9f);
        CHECK(fb->get_pixel(50, 51).r == 0.0f);
    }

    SUBCASE("Vertical line") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.line("v-line", {50, 0, 0}, {50, 100, 0}, Color::green());
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        CHECK(fb->get_pixel(50, 10).g > 0.9f);
        CHECK(fb->get_pixel(50, 50).g > 0.9f);
        CHECK(fb->get_pixel(50, 90).g > 0.9f);
        CHECK(fb->get_pixel(51, 50).g == 0.0f);
    }

    SUBCASE("Clipping test") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            // Line starting outside and ending outside
            s.line("clipped", {-50, 50, 0}, {150, 50, 0}, Color::white());
            return s.build();
        });
        // Should not crash and should render inside bounds
        auto fb = renderer.render_frame(comp, 0);
        CHECK(fb->get_pixel(0, 50).r > 0.9f);
        CHECK(fb->get_pixel(50, 50).r > 0.9f);
        CHECK(fb->get_pixel(99, 50).r > 0.9f);
    }
}
