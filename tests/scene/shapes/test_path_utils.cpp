#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/vector/path_factories.hpp>
#include <doctest/doctest.h>
#include <cmath>
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

TEST_CASE("Arrow factory emits a closed polygon and clamps malformed dimensions") {
    ArrowParams params;
    params.from = {0.0f, 0.0f};
    params.to = {100.0f, 0.0f};
    params.thickness = -4.0f;
    params.head_width = -20.0f;
    params.head_length = 500.0f;

    const PathParams arrow = make_arrow(params);
    REQUIRE(arrow.closed);
    REQUIRE(arrow.commands.size() == 8);
    for (const auto& command : arrow.commands) {
        CHECK(std::isfinite(command.p0.x));
        CHECK(std::isfinite(command.p0.y));
    }

    // A head longer than the shaft is clamped to the endpoint, so the tip
    // remains exactly at the requested destination rather than overshooting.
    CHECK(arrow.commands[3].p0.x == doctest::Approx(100.0f));
    CHECK(arrow.commands[3].p0.y == doctest::Approx(0.0f));
}

TEST_CASE("Path repeater expands deterministic copies with opacity decay") {
    PathShape path;
    path.commands = {PathCommand::move_to({1, 2}), PathCommand::line_to({3, 2})};
    path.fill = Fill::solid_color({1, 0, 0, 1});
    PathOperatorSpec spec;
    spec.kind = PathOperatorKind::Repeater;
    spec.params = RepeaterParams{3, {10, 0}, {1, 1}, 0, {0, 0}, 0.5f};
    const auto copies = apply_path_operators(path, {spec});
    REQUIRE(copies.size() == 3);
    CHECK(copies[0].commands[0].p0.x == doctest::Approx(1));
    CHECK(copies[1].commands[0].p0.x == doctest::Approx(11));
    CHECK(copies[2].commands[1].p0.x == doctest::Approx(23));
    CHECK(copies[0].fill.solid.a == doctest::Approx(1));
    CHECK(copies[1].fill.solid.a == doctest::Approx(0.5));
    CHECK(copies[2].fill.solid.a == doctest::Approx(0.25));
}

TEST_CASE("Round corners replaces polyline vertices with quadratic corners") {
    PathShape square;
    square.commands = {PathCommand::move_to({0, 0}), PathCommand::line_to({10, 0}),
        PathCommand::line_to({10, 10}), PathCommand::line_to({0, 10}),
        PathCommand::close()};
    square.closed = true;
    PathOperatorSpec op;
    op.kind = PathOperatorKind::RoundCorners;
    op.params = RoundCornersParams{2.0f};
    const auto result = apply_path_operators(square, {op});
    REQUIRE(result.size() == 1);
    CHECK(result[0].commands.size() == 9);
    CHECK(result[0].commands.front().type == PathCommandType::MoveTo);
    CHECK(result[0].commands[1].type == PathCommandType::QuadraticTo);
    CHECK(result[0].commands[1].p1.x == doctest::Approx(0));
    CHECK(result[0].commands[1].p1.y == doctest::Approx(0));
    CHECK(result[0].commands.back().type == PathCommandType::Close);
}

TEST_CASE("Zig-zag offsets a deterministic polyline by its local normal") {
    PathShape line;
    line.commands = {PathCommand::move_to({0, 0}), PathCommand::line_to({10, 0})};
    PathOperatorSpec op;
    op.kind = PathOperatorKind::ZigZag;
    op.params = ZigZagParams{2.0f, 0.2f, false};
    const auto result = apply_path_operators(line, {op});
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].commands.size() == 9);
    CHECK(result[0].commands[0].p0.x == doctest::Approx(0));
    CHECK(result[0].commands[0].p0.y == doctest::Approx(0));
    CHECK(result[0].commands[1].p0.x == doctest::Approx(1.25));
    CHECK(result[0].commands[1].p0.y == doctest::Approx(2));
    CHECK(result[0].commands.back().p0.x == doctest::Approx(10));
    CHECK(result[0].commands.back().p0.y == doctest::Approx(0));
    CHECK(result[0].commands[1].p0.y ==
          apply_path_operators(line, {op})[0].commands[1].p0.y);
}

TEST_CASE("Offset path applies signed normal displacement and preserves closure") {
    PathShape line;
    line.commands = {PathCommand::move_to({0, 0}), PathCommand::line_to({10, 0})};
    PathOperatorSpec op;
    op.kind = PathOperatorKind::OffsetPath;
    op.params = OffsetPathParams{2.0f, 4.0f};
    const auto result = apply_path_operators(line, {op});
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].commands.size() == 2);
    CHECK(result[0].commands[0].p0.x == doctest::Approx(0));
    CHECK(result[0].commands[0].p0.y == doctest::Approx(2));
    CHECK(result[0].commands[1].p0.x == doctest::Approx(10));
    CHECK(result[0].commands[1].p0.y == doctest::Approx(2));
}

TEST_CASE("Pucker-bloat transforms curve points and controls around its anchor") {
    PathShape curve;
    curve.commands = {PathCommand::move_to({0, 0}),
                      PathCommand::cubic_to({1, 0}, {1, 1}, {2, 1})};
    PathOperatorSpec op;
    op.kind = PathOperatorKind::PuckerBloat;
    op.params = PuckerBloatParams{0.5f, {1, 0}};
    const auto result = apply_path_operators(curve, {op});
    REQUIRE(result.size() == 1);
    CHECK(result[0].commands[0].p0.x == doctest::Approx(-0.5));
    CHECK(result[0].commands[1].p1.x == doctest::Approx(1));
    CHECK(result[0].commands[1].p2.y == doctest::Approx(1.5));
    CHECK(result[0].commands[1].p0.x == doctest::Approx(2.5));
}

TEST_CASE("Twist rotates farther points more while keeping its center fixed") {
    PathShape path;
    path.commands = {PathCommand::move_to({0, 0}), PathCommand::line_to({10, 0})};
    PathOperatorSpec op;
    op.kind = PathOperatorKind::Twist;
    op.params = TwistParams{90.0f, {0, 0}};
    const auto result = apply_path_operators(path, {op});
    REQUIRE(result.size() == 1);
    CHECK(result[0].commands[0].p0.x == doctest::Approx(0));
    CHECK(result[0].commands[0].p0.y == doctest::Approx(0));
    CHECK(result[0].commands[1].p0.x == doctest::Approx(0).epsilon(1e-5));
    CHECK(result[0].commands[1].p0.y == doctest::Approx(10));
}

TEST_CASE("Wiggle path is deterministic for a fixed seed and varies by seed") {
    PathShape line;
    line.commands = {PathCommand::move_to({0, 0}), PathCommand::line_to({10, 0})};
    PathOperatorSpec op;
    op.kind = PathOperatorKind::WigglePath;
    op.params = WigglePathParams{2.0f, 0.75f};
    op.seed = 1234;
    const auto first = apply_path_operators(line, {op});
    const auto repeated = apply_path_operators(line, {op});
    REQUIRE(first.size() == 1);
    CHECK(first[0].commands[0].p0.x == repeated[0].commands[0].p0.x);
    CHECK(first[0].commands[0].p0.y == repeated[0].commands[0].p0.y);
    op.seed = 5678;
    const auto other_seed = apply_path_operators(line, {op});
    CHECK(first[0].commands[0].p0.x != doctest::Approx(other_seed[0].commands[0].p0.x));
}
