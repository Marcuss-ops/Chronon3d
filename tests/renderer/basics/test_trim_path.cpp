#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include "tests/helpers/pixel_assertions.hpp"

using namespace chronon3d;
using namespace chronon3d::test;

static SoftwareRenderer g_rend;

static std::unique_ptr<Framebuffer> render_line(f32 trim_start, f32 trim_end) {
    Composition comp(CompositionSpec{.width = 100, .height = 10, .duration = 1},
        [=](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.line("l", {
                .from      = {5, 5, 0},
                .to        = {95, 5, 0},
                .thickness = 1.0f,
                .color     = Color{1, 1, 1, 1},
                .stroke    = {.trim_start = trim_start, .trim_end = trim_end},
            });
            return s.build();
        });
    return g_rend.render_frame(comp, 0);
}

// ---------------------------------------------------------------------------
// Full stroke (0..1) draws the entire line.
// ---------------------------------------------------------------------------
TEST_CASE("trim_path: full stroke draws complete line") {
    auto fb = render_line(0.0f, 1.0f);
    REQUIRE(fb != nullptr);

    const Color white{1, 1, 1, 1};
    const int w = width_at_row(*fb, 5, white);
    CHECK(w > 70);  // nearly 90px line on 100px canvas
}

// ---------------------------------------------------------------------------
// trim_end=0.5 draws approximately the first half.
// ---------------------------------------------------------------------------
TEST_CASE("trim_path: trim_end=0.5 draws first half") {
    auto fb = render_line(0.0f, 0.5f);
    REQUIRE(fb != nullptr);

    const Color white{1, 1, 1, 1};
    const int w = width_at_row(*fb, 5, white);
    CHECK(w > 15);   // some pixels drawn
    CHECK(w < 60);   // well short of full line

    // Second half of the canvas should be blank
    CHECK(!colors_match(fb->get_pixel(85, 5), white, 0.2f));
}

// ---------------------------------------------------------------------------
// trim_start=0.5 draws approximately the second half.
// ---------------------------------------------------------------------------
TEST_CASE("trim_path: trim_start=0.5 draws second half") {
    auto fb = render_line(0.5f, 1.0f);
    REQUIRE(fb != nullptr);

    const Color white{1, 1, 1, 1};

    // First quarter of the canvas should be blank
    CHECK(!colors_match(fb->get_pixel(10, 5), white, 0.2f));

    // Second half should have pixels
    CHECK(any_pixel(*fb, white));
}

// ---------------------------------------------------------------------------
// trim_start == trim_end → nothing drawn.
// ---------------------------------------------------------------------------
TEST_CASE("trim_path: trim_start==trim_end draws nothing") {
    auto fb = render_line(0.5f, 0.5f);
    REQUIRE(fb != nullptr);
    CHECK(!any_pixel(*fb, Color{1, 1, 1, 1}));
}

// ---------------------------------------------------------------------------
// trim_start > trim_end → nothing drawn (degenerate, clamped internally).
// ---------------------------------------------------------------------------
TEST_CASE("trim_path: trim_start > trim_end draws nothing") {
    auto fb = render_line(0.8f, 0.2f);
    REQUIRE(fb != nullptr);
    CHECK(!any_pixel(*fb, Color{1, 1, 1, 1}));
}
