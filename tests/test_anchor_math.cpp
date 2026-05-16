#include <doctest/doctest.h>
#include <chronon3d/layout/layout_solver.hpp>
#include <chronon3d/core/types.hpp>

using namespace chronon3d;

TEST_CASE("anchor resolve") {
    CHECK(anchor_position(Anchor::Center, 1920, 1080, 0) == Vec2{960, 540});
    CHECK(anchor_position(Anchor::TopLeft, 1920, 1080, 48) == Vec2{48, 48});
    CHECK(anchor_position(Anchor::BottomRight, 1280, 720, 20) == Vec2{1260, 700});
}
