#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/software/software_renderer.hpp>

using namespace chronon3d;

TEST_CASE("Draw order - Contro-test 3") {
    SoftwareRenderer renderer;
    CompositionSpec spec{.width = 100, .height = 100};
    Vec3 center = {50, 50, 0};
    Vec2 size = {40, 40};
    Vec2 smaller_size = {20, 20};

    SUBCASE("Test A: Red then Blue (smaller on top)") {
        Composition comp(spec, [&](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("red", center, Color::red(), size);
            s.rect("blue", center, Color::blue(), smaller_size);
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        Color c = fb->get_pixel(50, 50);
        
        CHECK(c.r == 0.0f);
        CHECK(c.b == 1.0f);
    }

    SUBCASE("Test B: Blue then Red (larger on top)") {
        Composition comp(spec, [&](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("blue", center, Color::blue(), smaller_size);
            s.rect("red", center, Color::red(), size);
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        Color c = fb->get_pixel(50, 50);
        
        CHECK(c.r == 1.0f);
        CHECK(c.b == 0.0f);
    }
}
