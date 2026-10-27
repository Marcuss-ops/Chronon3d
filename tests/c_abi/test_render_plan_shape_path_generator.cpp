#include <doctest/doctest.h>

#include <chronon3d/render_plan/shape_path_generator.hpp>

#include <stdexcept>
#include <limits>

using namespace chronon3d::render_plan;

TEST_CASE("procedural grid path generators enforce the named element cap") {
    const auto grid = make_grid_path(2047.0f, 2047.0f, 1.0f);
    CHECK(grid.size() == 8192);
    const auto dots = make_dot_grid_path(64.0f, 64.0f, 1.0f, 0.5f);
    // Eight perimeter vertices plus one Close command per generated dot.
    CHECK(dots.size() == kMaxGeneratedGridElements * 9U);
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

TEST_CASE("ring segment generator emits closed pie and annular sectors") {
    const auto pie = make_ring_segment_path(100.0f, 60.0f, 0.0f, 90.0f, 0.0f, 8);
    REQUIRE(pie.size() == 5);
    CHECK(pie.front().type == ShapePathCommandTypePlan::MoveTo);
    CHECK(pie[1].point[0] == doctest::Approx(35.355339f));
    CHECK(pie[1].point[1] == doctest::Approx(-21.213203f));
    CHECK(pie[3].point[0] == doctest::Approx(0.0f));
    CHECK(pie[3].point[1] == doctest::Approx(0.0f));
    CHECK(pie.back().type == ShapePathCommandTypePlan::Close);

    const auto donut = make_ring_segment_path(100.0f, 60.0f, 0.0f, 180.0f, 0.5f, 8);
    REQUIRE(donut.size() == 11);
    CHECK(donut[5].point[0] == doctest::Approx(0.0f));
    CHECK(donut[5].point[1] == doctest::Approx(15.0f));
    CHECK(donut.back().type == ShapePathCommandTypePlan::Close);
    CHECK_THROWS_AS(static_cast<void>(make_ring_segment_path(
        10.0f, 10.0f, 0.0f, 0.0f, 0.5f, 8)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(make_ring_segment_path(
        10.0f, 10.0f, 0.0f, 90.0f, 1.0f, 8)), std::invalid_argument);
}

TEST_CASE("monotone cubic path preserves data extrema and rejects invalid samples") {
    const std::array<std::array<float, 2>, 5> points{{
        {0.0f, 0.0f}, {1.0f, 1.0f}, {2.0f, 0.5f}, {3.0f, 0.5f}, {4.0f, 2.0f}}};
    const auto path = make_monotone_cubic_path(points);
    REQUIRE(path.size() == points.size());
    CHECK(path.front().type == ShapePathCommandTypePlan::MoveTo);
    CHECK(path[1].control1[1] == doctest::Approx(1.0f / 3.0f));
    CHECK(path[3].control1[1] == doctest::Approx(0.5f));
    CHECK(path[3].control2[1] == doctest::Approx(0.5f));
    for (std::size_t i = 1; i < path.size(); ++i) {
        const auto& command = path[i];
        REQUIRE(command.type == ShapePathCommandTypePlan::CubicTo);
        const float y0 = points[i - 1][1], y3 = points[i][1];
        const float low = std::min(y0, y3), high = std::max(y0, y3);
        float prior = y0;
        for (int step = 1; step <= 20; ++step) {
            const float t = static_cast<float>(step) / 20.0f;
            const float u = 1.0f - t;
            const float y = u*u*u*y0 + 3*u*u*t*command.control1[1] +
                3*u*t*t*command.control2[1] + t*t*t*y3;
            CHECK(y >= low - 1e-5f);
            CHECK(y <= high + 1e-5f);
            if (y3 >= y0) CHECK(y >= prior - 1e-5f);
            else CHECK(y <= prior + 1e-5f);
            prior = y;
        }
    }
    const std::array<std::array<float, 2>, 3> duplicate_x{{{0,0}, {1,1}, {1,2}}};
    CHECK_THROWS_AS(static_cast<void>(make_monotone_cubic_path(duplicate_x)), std::invalid_argument);
    const std::array<std::array<float, 2>, 2> non_finite{{{0,0}, {1,std::numeric_limits<float>::infinity()}}};
    CHECK_THROWS_AS(static_cast<void>(make_monotone_cubic_path(non_finite)), std::invalid_argument);
}
