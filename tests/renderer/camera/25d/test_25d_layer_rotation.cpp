#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <filesystem>

#include "tests/helpers/scene_fixtures.hpp"
#include "tests/helpers/pixel_assertions.hpp"
#include "tests/helpers/render_fixtures.hpp"

using namespace chronon3d;
using namespace chronon3d::test;

// ---------------------------------------------------------------------------
// Y-axis rotation narrows the projected width of a card.
// ---------------------------------------------------------------------------
TEST_CASE("3D layer Y rotation: rotated card is narrower than flat card") {
    const Color red{1, 0, 0, 1};

    auto flat = render_modular(single_layer_3d(red, {0, 0, 0}, {120, 120}));
    auto rot  = render_modular(single_layer_3d_rotated(red, {0, 0, 0}, {0, 45, 0}, {120, 120}));

    REQUIRE(flat != nullptr);
    REQUIRE(rot  != nullptr);

    CHECK(any_pixel(*flat, red));
    CHECK(any_pixel(*rot,  red));  // card must be visible, not blank

    const int mid_y = flat->height() / 2;
    int flat_w = width_at_row(*flat, mid_y, red);
    int rot_w  = width_at_row(*rot,  mid_y, red);

    CHECK(rot_w > 0);
    CHECK(rot_w < flat_w);  // 45° Y-rotation projects to a narrower span

    save_debug(*flat, "output/debug/25d/rotation_flat.png");
    save_debug(*rot,  "output/debug/25d/rotation_y45.png");
}

// ---------------------------------------------------------------------------
// X-axis rotation narrows the projected height.
// ---------------------------------------------------------------------------
TEST_CASE("3D layer X rotation: rotated card is shorter than flat card") {
    const Color blue{0, 0, 1, 1};

    auto flat = render_modular(single_layer_3d(blue, {0, 0, 0}, {120, 120}));
    auto rot  = render_modular(single_layer_3d_rotated(blue, {0, 0, 0}, {45, 0, 0}, {120, 120}));

    REQUIRE(flat != nullptr);
    REQUIRE(rot  != nullptr);

    CHECK(any_pixel(*rot, blue));

    const int mid_x = flat->width() / 2;
    int flat_h = height_at_col(*flat, mid_x, blue);
    int rot_h  = height_at_col(*rot,  mid_x, blue);

    CHECK(rot_h > 0);
    CHECK(rot_h < flat_h);

    save_debug(*rot, "output/debug/25d/rotation_x45.png");
}

// ---------------------------------------------------------------------------
// 90° rotation makes the card invisible (edge-on).
// ---------------------------------------------------------------------------
TEST_CASE("3D layer Y rotation 90deg: card is edge-on and nearly invisible") {
    const Color green{0, 1, 0, 1};

    auto rot90 = render_modular(single_layer_3d_rotated(green, {0, 0, 0}, {0, 90, 0}, {120, 120}));
    REQUIRE(rot90 != nullptr);

    const int mid_y = rot90->height() / 2;
    int w = width_at_row(*rot90, mid_y, green);

    // Edge-on: either invisible or at most a few pixels wide.
    CHECK(w <= 4);

    save_debug(*rot90, "output/debug/25d/rotation_y90.png");
}
