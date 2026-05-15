#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <Operations/background/dark_grid_background.hpp>

using namespace chronon3d;

TEST_CASE("Background grid is rendered directly from core APIs") {
    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(operations::background::dark_grid_background_scene(100, 100), 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 100);
    CHECK(fb->height() == 100);

    const Color off_grid = fb->get_pixel(10, 10);
    const Color center = fb->get_pixel(50, 50);
    CHECK(off_grid.r < 0.02f);
    CHECK(off_grid.g < 0.02f);
    CHECK(off_grid.b < 0.02f);

    bool has_grid = false;
    for (i32 y = 0; y < fb->height() && !has_grid; ++y) {
        for (i32 x = 0; x < fb->width(); ++x) {
            const Color p = fb->get_pixel(x, y);
            if (p.r > off_grid.r || p.g > off_grid.g || p.b > off_grid.b) {
                has_grid = true;
                break;
            }
        }
    }
    CHECK(has_grid);
}
