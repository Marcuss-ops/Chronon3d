#include <chronon3d/math/path_utils.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;
using namespace chronon3d::math;

TEST_CASE("Path Utilities: Flattening") {
    SUBCASE("Cubic Flattening") {
        Vec2 p0{0, 0};
        Vec2 p1{0, 100};
        Vec2 p2{100, 100};
        Vec2 p3{100, 0};
        auto points = flatten_cubic(p0, p1, p2, p3, 10);
        
        CHECK(points.size() == 11);
        CHECK(points.front().x == doctest::Approx(0.0f));
        CHECK(points.front().y == doctest::Approx(0.0f));
        CHECK(points.back().x == doctest::Approx(100.0f));
        CHECK(points.back().y == doctest::Approx(0.0f));
    }

    SUBCASE("Quadratic Flattening") {
        Vec2 p0{0, 0};
        Vec2 p1{50, 100};
        Vec2 p2{100, 0};
        auto points = flatten_quadratic(p0, p1, p2, 10);
        
        CHECK(points.size() == 11);
        CHECK(points.front().x == doctest::Approx(0.0f));
        CHECK(points.back().x == doctest::Approx(100.0f));
    }
}

TEST_CASE("Path Utilities: Polyline length") {
    std::vector<Vec2> points = {{0, 0}, {100, 0}, {100, 100}};
    CHECK(polyline_length(points) == doctest::Approx(200.0f));
}

TEST_CASE("Path Utilities: Trim Polyline") {
    std::vector<Vec2> points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    // Total length = 300
    
    SUBCASE("Full trim") {
        auto trimmed = trim_polyline(points, 0.0f, 1.0f);
        CHECK(polyline_length(trimmed) == doctest::Approx(300.0f));
        CHECK(trimmed.size() == 4);
    }
    
    SUBCASE("Partial trim from start") {
        auto trimmed = trim_polyline(points, 0.5f, 1.0f);
        // Length should be 150
        CHECK(polyline_length(trimmed) == doctest::Approx(150.0f));
        CHECK(trimmed.front().x == doctest::Approx(100.0f));
        CHECK(trimmed.front().y == doctest::Approx(50.0f));
        CHECK(trimmed.back().x == doctest::Approx(0.0f));
        CHECK(trimmed.back().y == doctest::Approx(100.0f));
    }
    
    SUBCASE("Partial trim from both ends") {
        auto trimmed = trim_polyline(points, 1.0f/3.0f, 2.0f/3.0f);
        // Middle segment: from (100, 0) to (100, 100)
        CHECK(polyline_length(trimmed) == doctest::Approx(100.0f));
        CHECK(trimmed.front().x == doctest::Approx(100.0f));
        CHECK(trimmed.front().y == doctest::Approx(0.0f));
        CHECK(trimmed.back().x == doctest::Approx(100.0f));
        CHECK(trimmed.back().y == doctest::Approx(100.0f));
    }

    SUBCASE("Empty trim") {
        auto trimmed = trim_polyline(points, 0.5f, 0.5f);
        CHECK(trimmed.empty());
        
        auto trimmed2 = trim_polyline(points, 0.7f, 0.3f);
        CHECK(trimmed2.empty());
    }
}

TEST_CASE("Path Utilities: Path Flattening") {
    PathShape path;
    path.commands.push_back(PathCommand::move_to({0, 0}));
    path.commands.push_back(PathCommand::line_to({100, 0}));
    path.commands.push_back(PathCommand::line_to({100, 100}));
    path.commands.push_back(PathCommand::close());
    
    auto subpaths = flatten_path(path, 10);
    CHECK(subpaths.size() == 1);
    CHECK(subpaths[0].size() == 4);
    CHECK(subpaths[0].back() == subpaths[0].front());
}
