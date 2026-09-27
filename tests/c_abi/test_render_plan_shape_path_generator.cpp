#include <doctest/doctest.h>

#include <chronon3d/render_plan/shape_path_generator.hpp>

#include <stdexcept>
#include <limits>

using namespace chronon3d::render_plan;

TEST_CASE("procedural grid path generators enforce the named element cap") {
    const auto grid = make_grid_path(2047.0f, 2047.0f, 1.0f);
    CHECK(grid.size() == 8192);
    const auto dots = make_dot_grid_path(64.0f, 64.0f, 1.0f, 0.5f);
    CHECK(dots.size() == kMaxGeneratedGridElements * 10U);
    CHECK_THROWS_AS(make_grid_path(2048.0f, 2048.0f, 1.0f), std::length_error);
    CHECK_THROWS_AS(make_dot_grid_path(65.0f, 65.0f, 1.0f, 0.5f), std::length_error);
    CHECK_THROWS_AS(make_grid_path(std::numeric_limits<float>::max(), 1.0f,
                                   std::numeric_limits<float>::min()), std::length_error);
}

TEST_CASE("procedural polygon path generation is deterministic and bounded") {
    const auto first = make_star_path(100.0f, 60.0f, 7, 0.4f, 15.0f);
    const auto repeated = make_star_path(100.0f, 60.0f, 7, 0.4f, 15.0f);
    REQUIRE(first.size() == 15);
    REQUIRE(repeated.size() == first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].type == repeated[i].type);
        CHECK(first[i].point[0] == doctest::Approx(repeated[i].point[0]));
        CHECK(first[i].point[1] == doctest::Approx(repeated[i].point[1]));
    }
    CHECK_THROWS_AS(make_polygon_path(100.0f, 60.0f, 2, 0.0f), std::invalid_argument);
    CHECK_THROWS_AS(make_arc_path(100.0f, 60.0f, 0.0f, 0.0f, 64), std::invalid_argument);
}
