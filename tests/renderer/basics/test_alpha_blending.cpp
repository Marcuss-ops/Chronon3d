#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/compositor/alpha.hpp>

using namespace chronon3d;

TEST_CASE("Alpha blending - Contro-test 1 (Pure Math)") {
    // Pure math blending tests (no renderer)
    
    SUBCASE("rosso alpha 0.5 sopra nero = rosso 0.5") {
        Color dst{0, 0, 0, 1};
        Color src{1, 0, 0, 0.5f};
        auto out = compositor::blend_normal(src, dst);
        CHECK(out.r == doctest::Approx(0.5f));
        CHECK(out.g == 0.0f);
        CHECK(out.b == 0.0f);
        CHECK(out.a == 1.0f);
    }

    SUBCASE("rosso alpha 1 sopra blu = rosso pieno") {
        Color dst{0, 0, 1, 1};
        Color src{1, 0, 0, 1.0f};
        auto out = compositor::blend_normal(src, dst);
        CHECK(out.r == 1.0f);
        CHECK(out.g == 0.0f);
        CHECK(out.b == 0.0f);
        CHECK(out.a == 1.0f);
    }

    SUBCASE("rosso alpha 0 sopra blu = blu invariato") {
        Color dst{0, 0, 1, 1};
        Color src{1, 0, 0, 0.0f};
        auto out = compositor::blend_normal(src, dst);
        CHECK(out.r == 0.0f);
        CHECK(out.g == 0.0f);
        CHECK(out.b == 1.0f);
        CHECK(out.a == 1.0f);
    }

    SUBCASE("verde alpha 0.5 sopra rosso alpha 1 = mix rosso/verde") {
        Color dst{1, 0, 0, 1};
        Color src{0, 1, 0, 0.5f};
        auto out = compositor::blend_normal(src, dst);
        CHECK(out.r == 0.5f);
        CHECK(out.g == 0.5f);
        CHECK(out.b == 0.0f);
    }
}

TEST_CASE("Alpha blending - Contro-test 2 (Renderer Overlap)") {
    SoftwareRenderer renderer;
    CompositionSpec spec{.width = 100, .height = 100};

    SUBCASE("Rect alpha overlap") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("red", {.size={50, 50}, .color=Color(1, 0, 0, 1), .pos={50, 50, 0}});
            s.rect("green", {.size={50, 50}, .color=Color(0, 1, 0, 0.5f), .pos={50, 50, 0}});
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        Color c = fb->get_pixel(50, 50);
        CHECK(c.r > 0.0f);
        CHECK(c.g > 0.0f);
        CHECK(c.b == 0.0f);
        CHECK(c.r == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(c.g == doctest::Approx(0.5f).epsilon(0.01));
    }

    SUBCASE("Circle alpha overlap") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.circle("red", {.radius=20.0f, .color=Color(1, 0, 0, 1), .pos={50, 50, 0}});
            s.circle("green", {.radius=20.0f, .color=Color(0, 1, 0, 0.5f), .pos={50, 50, 0}});
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        Color c = fb->get_pixel(50, 50);
        CHECK(c.r == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(c.g == doctest::Approx(0.5f).epsilon(0.01));
    }

    SUBCASE("Line alpha overlap") {
        Composition comp(spec, [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            // Draw a thick horizontal line (multiple lines)
            s.line("red", {.from={20, 50, 0}, .to={80, 50, 0}, .color=Color(1, 0, 0, 1)});
            s.line("green", {.from={20, 50, 0}, .to={80, 50, 0}, .color=Color(0, 1, 0, 0.5f)});
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        Color c = fb->get_pixel(50, 50);
        CHECK(c.r == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(c.g == doctest::Approx(0.5f).epsilon(0.01));
    }
}
