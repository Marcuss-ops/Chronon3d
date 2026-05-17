#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/fill.hpp>

#include "tests/helpers/pixel_assertions.hpp"

using namespace chronon3d;
using namespace chronon3d::test;

static std::unique_ptr<Framebuffer> render_scene(int w, int h,
    std::function<void(SceneBuilder&)> fn)
{
    SoftwareRenderer renderer;
    Composition comp(CompositionSpec{.width = w, .height = h, .duration = 1},
        [&](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            fn(s);
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

// ---------------------------------------------------------------------------
// Linear gradient: left red → right blue over a 100×20 rect.
// ---------------------------------------------------------------------------
TEST_CASE("linear gradient fill: left is red, right is blue") {
    auto fb = render_scene(100, 20, [](SceneBuilder& s) {
        s.rect("r", {
            .size  = {100, 20},
            .color = Color{1, 1, 1, 1},  // alpha carrier; gradient overrides rgb
            .pos   = {50, 10, 0},
            .fill  = Fill::linear({0, 0}, {1, 0}, {
                {0.0f, Color{1, 0, 0, 1}},
                {1.0f, Color{0, 0, 1, 1}},
            }),
        });
    });

    REQUIRE(fb != nullptr);

    const Color left  = fb->get_pixel(3,  10);
    const Color mid   = fb->get_pixel(50, 10);
    const Color right = fb->get_pixel(96, 10);

    // Left should be red-dominant
    CHECK(left.r > 0.6f);
    CHECK(left.b < 0.4f);

    // Right should be blue-dominant
    CHECK(right.b > 0.6f);
    CHECK(right.r < 0.4f);

    // Middle should be mixed (both channels non-trivial)
    CHECK(mid.r > 0.2f);
    CHECK(mid.b > 0.2f);
}

// ---------------------------------------------------------------------------
// Solid fill (no gradient): behaves identically to color field.
// ---------------------------------------------------------------------------
TEST_CASE("solid fill matches plain color") {
    auto fb_fill = render_scene(60, 60, [](SceneBuilder& s) {
        s.rect("r", {
            .size  = {60, 60},
            .color = Color{0, 1, 0, 1},
            .pos   = {30, 30, 0},
            .fill  = Fill::solid_color({0, 1, 0, 1}),
        });
    });
    auto fb_plain = render_scene(60, 60, [](SceneBuilder& s) {
        s.rect("r", {.size = {60, 60}, .color = {0, 1, 0, 1}, .pos = {30, 30, 0}});
    });

    REQUIRE(fb_fill != nullptr);
    REQUIRE(fb_plain != nullptr);

    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 60; ++x) {
            const Color a = fb_fill->get_pixel(x, y);
            const Color b = fb_plain->get_pixel(x, y);
            CHECK(a.r == doctest::Approx(b.r).epsilon(0.01f));
            CHECK(a.g == doctest::Approx(b.g).epsilon(0.01f));
            CHECK(a.b == doctest::Approx(b.b).epsilon(0.01f));
        }
    }
}

// ---------------------------------------------------------------------------
// Radial gradient: center white → edge black.
// ---------------------------------------------------------------------------
TEST_CASE("radial gradient fill: center is bright, edge is dark") {
    const int W = 100, H = 100;
    auto fb = render_scene(W, H, [](SceneBuilder& s) {
        s.circle("c", {
            .radius = 50.0f,
            .color  = Color{1, 1, 1, 1},
            .pos    = {50, 50, 0},
            .fill   = Fill::radial({0.5f, 0.5f}, 0.5f, {
                {0.0f, Color{1, 1, 1, 1}},
                {1.0f, Color{0, 0, 0, 1}},
            }),
        });
    });

    REQUIRE(fb != nullptr);

    const Color centre = fb->get_pixel(50, 50);
    const Color edge   = fb->get_pixel(50, 3);   // near top of circle

    CHECK(centre.r > 0.7f);   // bright at center
    CHECK(edge.r   < 0.5f);   // dark near edge
}

// ---------------------------------------------------------------------------
// Gradient with three stops: red → green → blue.
// ---------------------------------------------------------------------------
TEST_CASE("linear gradient fill: three-stop interpolation") {
    auto fb = render_scene(100, 20, [](SceneBuilder& s) {
        s.rect("r", {
            .size  = {100, 20},
            .color = Color{1, 1, 1, 1},
            .pos   = {50, 10, 0},
            .fill  = Fill::linear({0, 0}, {1, 0}, {
                {0.0f,  Color{1, 0, 0, 1}},
                {0.5f,  Color{0, 1, 0, 1}},
                {1.0f,  Color{0, 0, 1, 1}},
            }),
        });
    });

    REQUIRE(fb != nullptr);

    // Quarter point should be between red and green
    const Color q1 = fb->get_pixel(25, 10);
    CHECK(q1.r > 0.1f);
    CHECK(q1.g > 0.1f);
    CHECK(q1.b < 0.3f);

    // Three-quarter point should be between green and blue
    const Color q3 = fb->get_pixel(75, 10);
    CHECK(q3.g > 0.1f);
    CHECK(q3.b > 0.1f);
    CHECK(q3.r < 0.3f);
}
