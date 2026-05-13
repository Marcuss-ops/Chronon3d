#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/software/software_renderer.hpp>

using namespace chronon3d;

TEST_CASE("Coordinate system - Contro-test 4") {
    SoftwareRenderer renderer;
    CompositionSpec spec{.width = 100, .height = 100};

    SUBCASE("Rect center-based range [min, max)") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            // rect centro 50,50 size 20,20 -> x da 40 a 59, y da 40 a 59
            s.rect("box", {50, 50, 0}, Color::white(), {20, 20});
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        
        // centro: 50,50
        CHECK(fb->get_pixel(50, 50).r == 1.0f);
        
        // sinistra interna: 40,50
        CHECK(fb->get_pixel(40, 50).r == 1.0f);
        
        // destra interna: 59,50
        CHECK(fb->get_pixel(59, 50).r == 1.0f);
        
        // destra esterna: 60,50 (escluso secondo regola [min, max))
        CHECK(fb->get_pixel(60, 50).r == 0.0f);
        
        // fuori: 30,50 nero
        CHECK(fb->get_pixel(30, 50).r == 0.0f);
        
        // fuori: 70,50 nero
        CHECK(fb->get_pixel(70, 50).r == 0.0f);
    }

    SUBCASE("Circle center-based range") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.circle("orb", {50, 50, 0}, 10.0f, Color::white());
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        
        CHECK(fb->get_pixel(50, 50).r == 1.0f);
        CHECK(fb->get_pixel(60, 50).r == 1.0f); // dist = 10 <= 10
        CHECK(fb->get_pixel(61, 50).r == 0.0f); // dist = 11 > 10
    }
}
