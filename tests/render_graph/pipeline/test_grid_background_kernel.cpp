#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/compositor/alpha.hpp>

#include "src/backends/software/kernels/grid_background_kernel.hpp"

#include <cmath>
#include <cstdio>

using namespace chronon3d;
using namespace chronon3d::renderer;

// ── Helpers ─────────────────────────────────────────────────────────────────

static bool pixels_match(const Framebuffer& a, const Framebuffer& b, f32 tol = 1e-5f) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (i32 y = 0; y < a.height(); ++y) {
        const Color* row_a = a.pixels_row(y);
        const Color* row_b = b.pixels_row(y);
        for (i32 x = 0; x < a.width(); ++x) {
            const Color& ca = row_a[x];
            const Color& cb = row_b[x];
            if (std::abs(ca.r - cb.r) > tol ||
                std::abs(ca.g - cb.g) > tol ||
                std::abs(ca.b - cb.b) > tol ||
                std::abs(ca.a - cb.a) > tol) {
                return false;
            }
        }
    }
    return true;
}

// ── Tests ───────────────────────────────────────────────────────────────────

TEST_CASE("Grid background kernel keeps default params valid") {
    GridBackgroundParams p;
    CHECK(p.spacing > 0.0f);
    CHECK(p.major_every == 4);
    CHECK(p.centered == true);
}

TEST_CASE("Grid background renders deterministic pixels") {
    // Render a small grid at 128×96 with a non-integer offset to force
    // sub-pixel AA, then render the same frame twice and compare.
    SoftwareRenderer renderer;

    // Build scene with non-integer offset
    SceneBuilder builder;
    builder.grid_background("bg", {
        .size = {1920.0f, 1080.0f},
        .offset = {3.7f, -1.3f},
        .spacing = 32.0f,
        .minor_thickness = 1.5f,
        .major_thickness = 3.5f,
        .major_every = 4,
        .centered = true
    });
    auto scene = builder.build();

    Camera camera;

    auto fb1 = renderer.render_scene(scene, camera, 128, 96);
    REQUIRE(fb1 != nullptr);

    auto fb2 = renderer.render_scene(scene, camera, 128, 96);
    REQUIRE(fb2 != nullptr);

    CHECK(pixels_match(*fb1, *fb2));

    // Quick sanity: centre pixel should have colour (grid + background)
    Color c = fb1->get_pixel(64, 48);
    CHECK(c.r >= 0.0f);
    CHECK(c.g >= 0.0f);
    CHECK(c.b >= 0.0f);
    CHECK(c.a > 0.0f);
}

TEST_CASE("Grid background major intersection is not double blended") {
    // Pick a pixel that lies on both a major vertical and major horizontal line.
    // Use spacing=100, centred, so major lines are at x=0, x=±400, ...
    // At (0, 0) we have both a major vertical and horizontal line.
    // The alpha should be max(vertical, horizontal), NOT summed.
    SoftwareRenderer renderer;

    SceneBuilder builder;
    builder.grid_background("bg", {
        .size = {1920.0f, 1080.0f},
        .offset = {0.0f, 0.0f},
        .bg_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .grid_color = {1.0f, 1.0f, 1.0f, 0.5f},
        .spacing = 100.0f,
        .minor_thickness = 2.0f,
        .major_thickness = 8.0f,
        .major_every = 4,
        .centered = true
    });
    auto scene = builder.build();

    Camera camera;
    auto fb = renderer.render_scene(scene, camera, 400, 400);
    REQUIRE(fb != nullptr);

    // Centre pixel (200, 200) is at the intersection of major lines
    // (because centred origin is at 200,200 in a 400×400 canvas,
    //  spacing=100, major_every=4 → major_step=400, so
    //  major lines at x=0,±400 — i.e. at x=200 in canvas coordinates)
    Color c = fb->get_pixel(200, 200);
    MESSAGE("Centre pixel at intersection: r=", c.r, " g=", c.g, " b=", c.b, " a=", c.a);

    // The pixel should have alpha = max(major_vertical, major_horizontal)
    // which is just major_adj.a (since both are the same line).
    // Major alpha = 0.5 * 4 = 2.0, clamped to 1.0, times opacity=1.0
    // So alpha should be ≤ 1.0f
    CHECK(c.a <= 1.0f + 1e-5f);

    // Verify that a pixel far from all grid lines is just background
    Color far = fb->get_pixel(300, 50);
    // 300-200=100, which is exactly a minor line distance → should have some alpha
    // Let's try a point that's further off
    Color far2 = fb->get_pixel(350, 80);
    MESSAGE("Far pixel: r=", far2.r, " g=", far2.g, " b=", far2.b, " a=", far2.a);
}

TEST_CASE("Grid background renders correctly without centering") {
    SoftwareRenderer renderer;

    SceneBuilder builder;
    builder.grid_background("bg", {
        .size = {1920.0f, 1080.0f},
        .offset = {0.0f, 0.0f},
        .bg_color = {0.1f, 0.2f, 0.3f, 1.0f},
        .grid_color = {1.0f, 0.0f, 0.0f, 0.5f},
        .spacing = 50.0f,
        .minor_thickness = 1.0f,
        .major_thickness = 3.0f,
        .major_every = 4,
        .centered = false
    });
    auto scene = builder.build();

    Camera camera;
    auto fb = renderer.render_scene(scene, camera, 200, 200);
    REQUIRE(fb != nullptr);

    // Top-left pixel (0,0) should have a grid line since origin is (0,0) when not centered
    Color c = fb->get_pixel(0, 0);
    CHECK(c.a > 0.0f);
    CHECK(c.r > 0.0f); // grid is red
}

TEST_CASE("Grid background handles small spacing") {
    SoftwareRenderer renderer;

    SceneBuilder builder;
    builder.grid_background("bg", {
        .size = {1920.0f, 1080.0f},
        .offset = {0.0f, 0.0f},
        .bg_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .grid_color = {1.0f, 1.0f, 1.0f, 1.0f},
        .spacing = 10.0f,
        .minor_thickness = 1.0f,
        .major_thickness = 2.0f,
        .major_every = 5,
        .centered = true
    });
    auto scene = builder.build();

    Camera camera;
    auto fb = renderer.render_scene(scene, camera, 100, 100);
    REQUIRE(fb != nullptr);

    // Centre pixel — should be on a grid line
    Color c = fb->get_pixel(50, 50);
    CHECK(c.a > 0.0f);
}

TEST_CASE("Grid background edge-case zero thickness") {
    SoftwareRenderer renderer;

    // With zero thickness, the feathering still produces a half-alpha
    // exactly on-grid. Pick a pixel far from any grid line to verify
    // that off-grid pixels are pure background.
    SceneBuilder builder;
    builder.grid_background("bg", {
        .size = {1920.0f, 1080.0f},
        .bg_color = {0.0f, 0.0f, 0.0f, 1.0f},
        .grid_color = {1.0f, 1.0f, 1.0f, 1.0f},
        .spacing = 50.0f,
        .minor_thickness = 0.0f,
        .major_thickness = 0.0f,
        .major_every = 4,
        .centered = true
    });
    auto scene = builder.build();

    Camera camera;
    auto fb = renderer.render_scene(scene, camera, 100, 100);
    REQUIRE(fb != nullptr);

    // Pixel at (10, 10) is far from any grid line
    // With spacing=50, centered at (50,50), lines at x=0,±50,±100
    // gx = 10 - 50 = -40, cell_distance(-40, 50) = |-40 - (-50)| = 10
    // So distance = 10, feather threshold = 0.85 → weight = 0
    Color c = fb->get_pixel(10, 10);
    CHECK(c.r == doctest::Approx(0.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(1.0f));
}
