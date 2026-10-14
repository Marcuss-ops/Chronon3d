#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/vector/path_factories.hpp>
#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/graphics/vector_field2d.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include "src/backends/software/rasterizers/path/path_stroke.hpp"
#include <doctest/doctest.h>
#include <cmath>
#include <limits>
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

TEST_CASE("Odd dash patterns repeat before splitting a polyline") {
    const auto dashed = chronon3d::renderer::dash_polyline_points(
        {{0.0f, 0.0f}, {10.0f, 0.0f}}, false, {3.0f, 2.0f, 1.0f}, 0.0f);
    REQUIRE(dashed.size() == 3);
    CHECK(dashed[0].front().x == doctest::Approx(0.0f));
    CHECK(dashed[0].back().x == doctest::Approx(3.0f));
    CHECK(dashed[1].front().x == doctest::Approx(5.0f));
    CHECK(dashed[1].back().x == doctest::Approx(6.0f));
    CHECK(dashed[2].front().x == doctest::Approx(9.0f));
    CHECK(dashed[2].back().x == doctest::Approx(10.0f));
}

TEST_CASE("Tapered path segment interpolates stroke radius by arc length") {
    CHECK(chronon3d::renderer::segment_coverage(
        {1.0f, 2.0f}, {0.0f, 0.0f}, {10.0f, 0.0f}, 0.0f, 5.0f) == doctest::Approx(0.0f));
    CHECK(chronon3d::renderer::segment_coverage(
        {9.0f, 2.0f}, {0.0f, 0.0f}, {10.0f, 0.0f}, 0.0f, 5.0f) == doctest::Approx(1.0f));
}

TEST_CASE("Field2D generators and operators are deterministic and bounded") {
    using namespace chronon3d::graphics;
    constexpr std::array generators{
        Field2DGenerator::Perlin, Field2DGenerator::Simplex, Field2DGenerator::Fractal,
        Field2DGenerator::Cellular, Field2DGenerator::Grid, Field2DGenerator::Checker,
        Field2DGenerator::Dots, Field2DGenerator::Stripes, Field2DGenerator::Rings,
        Field2DGenerator::Linear, Field2DGenerator::Radial, Field2DGenerator::Conic,
        Field2DGenerator::Burst, Field2DGenerator::Value, Field2DGenerator::Ridged};
    for (const auto generator : generators) {
        Field2D field;
        field.generator = generator;
        field.seed = 73;
        field.frequency = 5.0f;
        field.operators = {{.kind = Field2DOperatorKind::Warp, .amount = 0.2f},
                           {.kind = Field2DOperatorKind::Smoothstep, .edge = .5f, .softness = .3f},
                           {.kind = Field2DOperatorKind::Invert}};
        const float a = field.sample({0.23f, 0.71f});
        const float b = field.sample({0.23f, 0.71f});
        CHECK(std::isfinite(a));
        CHECK(a >= 0.0f);
        CHECK(a <= 1.0f);
        CHECK(a == b);
    }

    Field2D linear;
    linear.generator = Field2DGenerator::Linear;
    CHECK(linear.sample({0.25f, 0.9f}) == doctest::Approx(0.25f));
    linear.operators = {{.kind = Field2DOperatorKind::Threshold, .edge = 0.5f}};
    CHECK(linear.sample({0.25f, 0.9f}) == doctest::Approx(0.0f));
    CHECK(linear.sample({0.75f, 0.1f}) == doctest::Approx(1.0f));
}

TEST_CASE("Curl noise evolves continuously from the canonical scalar potential") {
    chronon3d::graphics::VectorField2D field;
    field.type=chronon3d::graphics::VectorFieldType::CurlNoise;
    field.seed=12045;
    const auto first=field.sample({.21f,.37f},0.f);
    const auto next=field.sample({.21f,.37f},.001f);
    const auto later=field.sample({.21f,.37f},2.f);
    CHECK(glm::length(first-next)<.01f);
    CHECK(glm::length(first-later)>1.e-6f);
    CHECK(later==field.sample({.21f,.37f},2.f));
    field.speed=std::numeric_limits<float>::infinity();
    CHECK(field.sample({.21f,.37f},2.f)==Vec2(0.f));
}

TEST_CASE("Field2D temporal drift is deterministic under random frame access") {
    chronon3d::graphics::Field2D field;
    field.generator = chronon3d::graphics::Field2DGenerator::Fractal;
    field.seed = 812;
    field.frequency = 2.6f;
    field.octaves = 4;
    const auto frame_at_two_seconds = field.drifted(2.0, {0.08f, -0.04f});
    const auto direct_frame_at_two_seconds = field.drifted(2.0, {0.08f, -0.04f});
    CHECK(chronon3d::graphics::field2d_fingerprint(frame_at_two_seconds) ==
          chronon3d::graphics::field2d_fingerprint(direct_frame_at_two_seconds));
    CHECK(frame_at_two_seconds.sample({0.3f, 0.6f}) ==
          direct_frame_at_two_seconds.sample({0.3f, 0.6f}));
    CHECK(chronon3d::graphics::field2d_fingerprint(frame_at_two_seconds) !=
          chronon3d::graphics::field2d_fingerprint(field));
    CHECK_THROWS_AS(([&] {
        (void)field.drifted(std::numeric_limits<double>::infinity(), {0, 0});
    }()), std::invalid_argument);
}

TEST_CASE("Field2D is retained by the canonical FillStyle bridge") {
    chronon3d::graphics::Field2D field;
    field.generator = chronon3d::graphics::Field2DGenerator::Linear;
    chronon3d::graphics::FillStyle style =
        chronon3d::graphics::FillStyle::solid({0.8f, 0.4f, 0.2f, 0.75f});
    style.field = field;
    const auto runtime = style.to_fill();
    CHECK(runtime.field.has_value());
    const auto sampled = chronon3d::sample_fill_at(runtime, {0.25f, 0.9f});
    CHECK(sampled.r == doctest::Approx(0.2f));
    CHECK(sampled.g == doctest::Approx(0.1f));
    CHECK(sampled.b == doctest::Approx(0.05f));
    CHECK(sampled.a == doctest::Approx(0.75f));
}

TEST_CASE("Field2D ColorRamp maps scalar values to authored colors") {
    chronon3d::graphics::Field2D field;
    field.generator = chronon3d::graphics::Field2DGenerator::Linear;
    auto ramp = chronon3d::graphics::GradientDefinition::linear(
        {0.0f, 0.5f}, {1.0f, 0.5f},
        {{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
         {0.5f, {1.0f, 0.0f, 0.0f, 1.0f}},
         {1.0f, {1.0f, 1.0f, 1.0f, 1.0f}}});
    auto style = chronon3d::graphics::FillStyle::solid({1.0f, 1.0f, 1.0f, 1.0f});
    style.field = field;
    style.field_ramp = ramp;
    const auto runtime = style.to_fill();
    REQUIRE(runtime.field_ramp.has_value());
    const auto sampled = chronon3d::sample_fill_at(runtime, {0.25f, 0.9f});
    CHECK(sampled.r == doctest::Approx(0.5f));
    CHECK(sampled.g == doctest::Approx(0.0f));
    CHECK(sampled.b == doctest::Approx(0.0f));
    CHECK(sampled.a == doctest::Approx(1.0f));
    const auto first = chronon3d::sample_fill_at(runtime, {0.0f, 0.5f});
    const auto last = chronon3d::sample_fill_at(runtime, {1.0f, 0.5f});
    CHECK(first.r == doctest::Approx(0.0f));
    CHECK(first.g == doctest::Approx(0.0f));
    CHECK(last.r == doctest::Approx(1.0f));
    CHECK(last.g == doctest::Approx(1.0f));
}

TEST_CASE("Field2D low-resolution grid interpolates a dense deterministic field") {
    chronon3d::graphics::Field2D field;
    field.generator = chronon3d::graphics::Field2DGenerator::Fractal;
    field.seed = 29;
    field.frequency = 2.4f;
    field.octaves = 3;
    auto low_res = chronon3d::Fill::field2d(
        field, {1.0f, 1.0f, 1.0f, 1.0f}, std::nullopt, 4, 256, 144);
    REQUIRE(low_res.field_grid.has_value());
    CHECK(low_res.field_grid->width == 64);
    CHECK(low_res.field_grid->height == 36);
    double error_sum = 0.0;
    float max_error = 0.0f;
    constexpr int samples = 101;
    for (int i = 0; i < samples; ++i) {
        const float x = static_cast<float>((i * 37) % samples) / (samples - 1);
        const float y = static_cast<float>((i * 61) % samples) / (samples - 1);
        const float reference = field.sample({x, y});
        const float interpolated = low_res.field_grid->sample({x, y});
        const float error = std::abs(reference - interpolated);
        error_sum += error;
        max_error = std::max(max_error, error);
    }
    CHECK(error_sum / samples < 0.025);
    CHECK(max_error < 0.12f);
    CHECK_THROWS_AS(chronon3d::Fill::field2d(field, {1, 1, 1, 1}, std::nullopt,
                                             3, 256, 144), std::invalid_argument);
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

TEST_CASE("Merge combines paths as independent subpaths without boolean operations") {
    PathShape base;
    base.commands = {PathCommand::move_to({0, 0}), PathCommand::line_to({4, 0})};
    MergeParams merge;
    merge.paths.push_back({PathCommand::move_to({10, 10}),
                           PathCommand::quadratic_to({12, 10}, {12, 12}),
                           PathCommand::close()});
    PathOperatorSpec op;
    op.kind = PathOperatorKind::Merge;
    op.params = std::move(merge);
    const auto result = apply_path_operators(base, {op});
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].commands.size() == 5);
    CHECK(result[0].commands[2].type == PathCommandType::MoveTo);
    CHECK(result[0].commands[3].type == PathCommandType::QuadraticTo);
    CHECK(result[0].commands[4].type == PathCommandType::Close);
}

TEST_CASE("Path morph interpolates command coordinates without changing contour topology") {
    PathShape source;
    source.commands = {
        PathCommand::move_to({0.0f, 0.0f}),
        PathCommand::cubic_to({2.0f, 0.0f}, {2.0f, 2.0f}, {4.0f, 2.0f}),
        PathCommand::close(),
        PathCommand::move_to({10.0f, 10.0f}),
        PathCommand::quadratic_to({12.0f, 10.0f}, {12.0f, 12.0f}),
        PathCommand::close()};
    source.closed = true;
    MorphPathParams params;
    params.target = {
        PathCommand::move_to({2.0f, 4.0f}),
        PathCommand::cubic_to({6.0f, 4.0f}, {6.0f, 8.0f}, {10.0f, 8.0f}),
        PathCommand::close(),
        PathCommand::move_to({20.0f, 20.0f}),
        PathCommand::quadratic_to({24.0f, 20.0f}, {24.0f, 24.0f}),
        PathCommand::close()};
    params.progress = 0.5f;

    const auto result = apply_path_operators(source, {{PathOperatorKind::Morph, params, 0}});
    REQUIRE(result.size() == 1);
    CHECK(result[0].commands.size() == source.commands.size());
    CHECK(result[0].commands[0].p0 == Vec2{1.0f, 2.0f});
    CHECK(result[0].commands[1].p1 == Vec2{4.0f, 2.0f});
    CHECK(result[0].commands[1].p2 == Vec2{4.0f, 5.0f});
    CHECK(result[0].commands[1].p0 == Vec2{7.0f, 5.0f});
    CHECK(result[0].commands[3].p0 == Vec2{15.0f, 15.0f});
    CHECK(result[0].commands[4].p1 == Vec2{18.0f, 15.0f});
    CHECK(result[0].commands[4].p0 == Vec2{18.0f, 18.0f});
    CHECK(result[0].commands[2].type == PathCommandType::Close);
    CHECK(result[0].closed);

    params.progress = 0.0f;
    const auto start = apply_path_operators(source, {{PathOperatorKind::Morph, params, 0}});
    CHECK(start[0].commands[1].p0 == source.commands[1].p0);
    params.progress = 1.0f;
    const auto end = apply_path_operators(source, {{PathOperatorKind::Morph, params, 0}});
    CHECK(end[0].commands[1].p0 == params.target[1].p0);

    // Opposite-sign finite float endpoints must not overflow in the
    // interpolation delta (the midpoint is representable and finite).
    PathShape extreme;
    extreme.commands = {
        PathCommand::move_to({-std::numeric_limits<float>::max(), 0.0f}),
        PathCommand::line_to({-std::numeric_limits<float>::max(), 1.0f})};
    MorphPathParams extreme_params{
        {PathCommand::move_to({std::numeric_limits<float>::max(), 0.0f}),
         PathCommand::line_to({std::numeric_limits<float>::max(), 1.0f})},
        0.5f};
    const auto midpoint = apply_path_operators(
        extreme, {{PathOperatorKind::Morph, extreme_params, 0}});
    CHECK(std::isfinite(midpoint[0].commands[0].p0.x));
    CHECK(midpoint[0].commands[0].p0.x == doctest::Approx(0.0f));
}

TEST_CASE("Path morph fails closed for invalid progress, topology, contours and coordinates") {
    PathShape source;
    source.commands = {PathCommand::move_to({0.0f, 0.0f}), PathCommand::line_to({2.0f, 0.0f})};
    PathOperatorSpec op{PathOperatorKind::Morph,
        MorphPathParams{{PathCommand::move_to({0.0f, 0.0f}),
                         PathCommand::line_to({4.0f, 0.0f})}, 0.5f}, 0};
    CHECK_NOTHROW(apply_path_operators(source, {op}));
    op.params = MorphPathParams{{PathCommand::move_to({0.0f, 0.0f}),
                                 PathCommand::cubic_to({1.0f, 0.0f}, {2.0f, 0.0f}, {4.0f, 0.0f})}, 0.5f};
    CHECK_THROWS(apply_path_operators(source, {op}));
    op.params = MorphPathParams{{PathCommand::line_to({0.0f, 0.0f}),
                                 PathCommand::line_to({4.0f, 0.0f})}, 0.5f};
    CHECK_THROWS(apply_path_operators(source, {op}));
    op.params = MorphPathParams{{PathCommand::move_to({0.0f, 0.0f}),
                                 PathCommand::line_to({4.0f, 0.0f})}, 1.1f};
    CHECK_THROWS(apply_path_operators(source, {op}));
    op.params = MorphPathParams{{PathCommand::move_to({0.0f, 0.0f}),
                                 PathCommand::line_to({std::numeric_limits<float>::infinity(), 0.0f})}, 0.5f};
    CHECK_THROWS(apply_path_operators(source, {op}));
    op.params = MorphPathParams{{PathCommand::move_to({0.0f, 0.0f}),
                                 PathCommand::close()}, 0.5f};
    CHECK_THROWS(apply_path_operators(source, {op}));
}

TEST_CASE("Trim paths keeps the requested arc-length window") {
    PathShape square;
    square.commands = {PathCommand::move_to({0, 0}), PathCommand::line_to({100, 0}),
        PathCommand::line_to({100, 100}), PathCommand::line_to({0, 100}),
        PathCommand::close()};
    square.closed = true;

    SUBCASE("Full window is the identity") {
        PathOperatorSpec op;
        op.kind = PathOperatorKind::Trim;
        op.params = TrimPathParams{0.0f, 1.0f, 0.3f};
        const auto result = apply_path_operators(square, {op});
        REQUIRE(result.size() == 1);
        CHECK(result[0].commands.size() == square.commands.size());
        CHECK(result[0].commands.back().type == PathCommandType::Close);
    }

    SUBCASE("Half window trims half the perimeter") {
        PathOperatorSpec op;
        op.kind = PathOperatorKind::Trim;
        op.params = TrimPathParams{0.0f, 0.5f, 0.0f};
        const auto result = apply_path_operators(square, {op});
        REQUIRE(result.size() == 1);
        std::vector<Vec2> points;
        for (const auto& command : result[0].commands) points.push_back(command.p0);
        CHECK(math::polyline_length(points) == doctest::Approx(200.0f));
        CHECK(result[0].commands.front().p0.x == doctest::Approx(0.0f));
        CHECK(result[0].commands.front().p0.y == doctest::Approx(0.0f));
        CHECK(result[0].commands.back().p0.x == doctest::Approx(100.0f));
        CHECK(result[0].commands.back().p0.y == doctest::Approx(100.0f));
    }

    SUBCASE("Offset slides the window along the path") {
        PathOperatorSpec op;
        op.kind = PathOperatorKind::Trim;
        op.params = TrimPathParams{0.0f, 0.25f, 0.5f};
        const auto result = apply_path_operators(square, {op});
        REQUIRE(result.size() == 1);
        std::vector<Vec2> points;
        for (const auto& command : result[0].commands) points.push_back(command.p0);
        CHECK(math::polyline_length(points) == doctest::Approx(100.0f));
        CHECK(result[0].commands.front().p0.x == doctest::Approx(100.0f));
        CHECK(result[0].commands.front().p0.y == doctest::Approx(100.0f));
        CHECK(result[0].commands.back().p0.x == doctest::Approx(0.0f));
        CHECK(result[0].commands.back().p0.y == doctest::Approx(100.0f));
    }

    SUBCASE("Window wrapping emits two runs without a connecting segment") {
        PathOperatorSpec op;
        op.kind = PathOperatorKind::Trim;
        op.params = TrimPathParams{0.0f, 0.25f, 0.8f};
        const auto result = apply_path_operators(square, {op});
        REQUIRE(result.size() == 1);
        std::size_t moves = 0;
        for (const auto& command : result[0].commands)
            if (command.type == PathCommandType::MoveTo) ++moves;
        CHECK(moves == 2);
        std::vector<Vec2> points;
        for (const auto& command : result[0].commands) points.push_back(command.p0);
        CHECK(math::polyline_length(points) == doctest::Approx(100.0f));
    }

    SUBCASE("Empty window yields an empty path") {
        PathOperatorSpec op;
        op.kind = PathOperatorKind::Trim;
        op.params = TrimPathParams{0.5f, 0.5f, 0.0f};
        const auto result = apply_path_operators(square, {op});
        REQUIRE(result.size() == 1);
        CHECK(result[0].commands.empty());
    }

    SUBCASE("Invalid parameters are rejected") {
        PathOperatorSpec op;
        op.kind = PathOperatorKind::Trim;
        op.params = TrimPathParams{0.8f, 0.2f, 0.0f};
        CHECK_THROWS(apply_path_operators(square, {op}));
        op.params = TrimPathParams{0.0f, 1.5f, 0.0f};
        CHECK_THROWS(apply_path_operators(square, {op}));
        op.params = TrimPathParams{0.0f, 0.5f, std::numeric_limits<float>::quiet_NaN()};
        CHECK_THROWS(apply_path_operators(square, {op}));
    }
}
