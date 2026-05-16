#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

using namespace chronon3d;

TEST_CASE("Background grid is rendered directly from core APIs") {
    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(scene::utils::dark_grid_background_scene(100, 100), 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 100);
    CHECK(fb->height() == 100);

    // Background is near-black everywhere
    const Color bg = fb->get_pixel(10, 10);
    CHECK(bg.r < 0.02f);
    CHECK(bg.g < 0.02f);
    CHECK(bg.b < 0.02f);

    // Grid lines (spacing=80) appear at x=0 and x=80. Line at x=80 should be brighter.
    const Color on_grid = fb->get_pixel(80, 50);
    CHECK(on_grid.r > bg.r);
}
