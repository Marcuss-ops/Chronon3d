#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/software/software_renderer.hpp>

using namespace chronon3d;

TEST_CASE("Clipping - Contro-test 5") {
    SoftwareRenderer renderer;
    CompositionSpec spec{.width = 100, .height = 100};

    SUBCASE("Rect offscreen (partially)") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            // Center -10, 50, size 40x40 -> x from -30 to 10
            s.rect("offscreen", {-10, 50, 0}, Color::white(), {40, 40});
            return s.build();
        });
        // Should not crash
        auto fb = renderer.render_frame(comp, 0);
        // Check visible part
        CHECK(fb->get_pixel(0, 50).r == 1.0f);
        CHECK(fb->get_pixel(9, 50).r == 1.0f);
    }

    SUBCASE("Circle offscreen") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.circle("circle", {0, 0, 0}, 30, Color::white());
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        CHECK(fb->get_pixel(0, 0).r == 1.0f);
        CHECK(fb->get_pixel(29, 0).r == 1.0f);
    }

    SUBCASE("Line offscreen") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.line("line", {-100, -100, 0}, {600, 600, 0}, Color::white());
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        // Line from -100,-100 to 600,600 passes through 50,50
        CHECK(fb->get_pixel(50, 50).r == 1.0f);
    }
}
