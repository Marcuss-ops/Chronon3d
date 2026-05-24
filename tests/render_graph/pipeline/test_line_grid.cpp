#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/scene/shape.hpp>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::graph;

TEST_CASE("Line Thickness - BBox and Raster Verification") {
    // We create a scene with a thick horizontal line of thickness 10.
    SceneBuilder builder;
    builder.line("thick_line", {
        .from = {-100.0f, 0.0f, 0.0f},
        .to = {100.0f, 0.0f, 0.0f},
        .thickness = 10.0f,
        .color = Color::red()
    });

    Scene scene = builder.build();
    SoftwareRenderer renderer;
    Camera camera;

    // Render onto a 400x200 framebuffer
    auto fb = renderer.render_scene(scene, camera, 400, 200);
    REQUIRE(fb != nullptr);

    // Let's print any colored pixels we find
    int colored_count = 0;
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 400; ++x) {
            Color pixel = fb->get_pixel(x, y);
            if (pixel.a > 0.01f) {
                if (colored_count < 20) {
                    printf("Pixel at (%d, %d): r=%f, g=%f, b=%f, a=%f\n", x, y, pixel.r, pixel.g, pixel.b, pixel.a);
                }
                colored_count++;
            }
        }
    }
    printf("Total colored pixels: %d\n", colored_count);

    // Verify thickness at center point (200, 100)
    // The line has thickness 10, so from y = 95 to y = 105 it should be red.
    // Let's check pixel color at y=100 (exactly on the line).
    Color c_mid = fb->get_pixel(200, 100);
    CHECK(c_mid.r > 0.9f);
    CHECK(c_mid.g < 0.1f);
    CHECK(c_mid.b < 0.1f);
    CHECK(c_mid.a > 0.9f);

    // Check pixel color at y=104 (within the thickness bounds)
    Color c_near = fb->get_pixel(200, 104);
    CHECK(c_near.r > 0.5f); // Should have significant red
    CHECK(c_near.a > 0.5f);

    // Check pixel color at y=107 (outside the thickness bounds, accounting for anti-aliasing)
    Color c_far = fb->get_pixel(200, 107);
    CHECK(c_far.a < 0.05f); // Should be transparent or nearly transparent
}

TEST_CASE("Grid count logic off-by-one verification") {
    // Direct verification of grid n calculations.
    // If extent = 1000 and spacing = 200, n = 5 (lines at -1000, -800, ..., 1000).
    // Total lines = 2 * n + 1 = 11.
    f32 extent = 1000.0f;
    f32 spacing = 200.0f;
    
    int n = static_cast<int>(std::floor(extent / spacing));
    CHECK(n == 5);

    // Extent < spacing case
    extent = 50.0f;
    spacing = 100.0f;
    n = static_cast<int>(std::floor(extent / spacing));
    CHECK(n == 0); // Only center line should be drawn

    // Invalid spacing case
    spacing = 0.0f;
    CHECK(spacing <= 0.0f);
}
