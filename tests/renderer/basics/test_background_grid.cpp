#include <doctest/doctest.h>

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

using namespace chronon3d;

TEST_CASE("Background grid is rendered directly from core APIs") {
    // Render the grid directly into a framebuffer (no disk I/O or PNG cache needed)
    DarkGridBgParams params;
    Framebuffer fb(100, 100);
    scene::utils::detail::rasterize_dark_grid_background(fb, params);

    CHECK(fb.width() == 100);
    CHECK(fb.height() == 100);

    // Background is near-black everywhere
    // (bg_color default = {0.098, 0.098, 0.11} sRGB → ~0.0073 linear after to_linear())
    const Color bg = fb.get_pixel(10, 10);
    CHECK(bg.r < 0.02f);
    CHECK(bg.g < 0.02f);
    CHECK(bg.b < 0.02f);

    // Grid lines (spacing=80) appear at x=0 and x=80. Line at x=80 should be brighter.
    const Color on_grid = fb.get_pixel(80, 50);
    CHECK(on_grid.r > bg.r);
}
