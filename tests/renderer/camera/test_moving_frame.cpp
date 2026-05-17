#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/animation/interpolate.hpp>

using namespace chronon3d;

TEST_CASE("Moving frame - Contro-test 7") {
    SoftwareRenderer renderer;
    CompositionSpec spec{.width = 200, .height = 100};

    auto scene_func = [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        auto x = interpolate(ctx.frame, 0, 60, 50.0f, 150.0f);
        s.rect("moving", {.size={20, 20}, .color=Color::white(), .pos={x, 50, 0}});
        return s.build();
    };

    Composition comp(spec, scene_func);

    SUBCASE("Frame 0: pixel near x=50 is white") {
        auto fb = renderer.render_frame(comp, 0);
        CHECK(fb->get_pixel(50, 50).r == 1.0f);
        CHECK(fb->get_pixel(150, 50).r == 0.0f);
    }

    SUBCASE("Frame 60: pixel near x=150 is white") {
        auto fb = renderer.render_frame(comp, 60);
        CHECK(fb->get_pixel(150, 50).r == 1.0f);
        CHECK(fb->get_pixel(50, 50).r == 0.0f);
    }

    SUBCASE("Frames are different") {
        auto fb0 = renderer.render_frame(comp, 0);
        auto fb60 = renderer.render_frame(comp, 60);
        
        bool different = false;
        for (int x = 0; x < 200; ++x) {
            if (fb0->get_pixel(x, 50).r != fb60->get_pixel(x, 50).r) {
                different = true;
                break;
            }
        }
        CHECK(different == true);
    }
}
