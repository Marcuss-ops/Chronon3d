#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include "test_render_plan_shape_helpers.hpp"

#include <nlohmann/json.hpp>
#include <string>

using namespace chronon3d;

TEST_CASE("V3 monotone_line decodes data points into canonical path commands") {
    auto source = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "monotone_line"},
        {"data_points", {{0.0, 2.0}, {1.0, 4.0}, {2.0, 3.0}}},
        {"stroke", {{"color", "#FFFFFF"}, {"width", 2.0}}}})});
    const auto decoded = render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].shape);
    REQUIRE(decoded->layers[0].shape->path.size() == 3);
    CHECK(decoded->layers[0].shape->path[1].type ==
          render_plan::ShapePathCommandTypePlan::CubicTo);
    auto changed = source;
    changed["layers"][0]["shape"]["data_points"][1][1] = 5.0;
    const auto changed_plan = render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(changed_plan->content_fingerprint != decoded->content_fingerprint);
    auto invalid = source;
    invalid["layers"][0]["shape"]["data_points"][2][0] = 0.0;
    CHECK_FALSE(render_plan::decode_render_plan(invalid));
    const auto no_stroke = render_plan_test::shape_layer({
        {"type", "monotone_line"}, {"data_points", {{0.0, 0.0}, {1.0, 1.0}}}});
    CHECK_FALSE(render_plan::decode_render_plan(render_plan_test::base_plan({no_stroke})));
}

TEST_CASE("V3 conic gradients decode through the existing software fill contract") {
    auto source = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "ellipse"},
        {"fill", {{"type", "conic"}, {"center", {0.5, 0.5}}, {"angle", 0.25},
            {"color_stops", {{{"position", 0.0}, {"color", {1.0, 0.0, 0.0, 1.0}}},
                              {{"position", 1.0}, {"color", {0.0, 0.0, 1.0, 1.0}}}}}}}})});
    const auto decoded = render_plan::decode_render_plan(source);
    const std::string decode_error = decoded.has_value() ? std::string{} : decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_error);
    REQUIRE(decoded->layers[0].shape);
    REQUIRE(decoded->layers[0].shape->gradient);
    CHECK(decoded->layers[0].shape->gradient->type == graphics::GradientType::Conic);
    auto changed = source;
    changed["layers"][0]["shape"]["fill"]["angle"] = 0.5;
    const auto changed_decoded = render_plan::decode_render_plan(changed);
    REQUIRE(changed_decoded);
    CHECK(decoded->content_fingerprint != changed_decoded->content_fingerprint);
}

TEST_CASE("V3 monotone_chart exposes filled area, grid, line, and axes with stable fingerprints") {
    auto source = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "monotone_chart"},
        {"data_points", {{0.0, 2.0}, {5.0, 8.0}, {10.0, 4.0}}},
        {"x_domain", {0.0, 10.0}}, {"y_domain", {0.0, 10.0}},
        {"x_ticks", 5}, {"y_ticks", 5}, {"show_grid", true}, {"show_axes", true}})});
    const auto decoded = render_plan::decode_render_plan(source);
    const std::string error = decoded ? std::string{} : decoded.error().message;
    REQUIRE_MESSAGE(decoded, error);
    const auto& shape = *decoded->layers[0].shape;
    REQUIRE(shape.type == render_plan::ShapeContentTypePlan::MonotoneChart);
    REQUIRE(shape.imported_subpaths.size() == 4);
    CHECK_FALSE(shape.imported_subpaths[0].path.empty()); // grid
    REQUIRE(shape.imported_subpaths[1].fill_color);       // area
    CHECK(shape.imported_subpaths[1].path.back().type ==
          render_plan::ShapePathCommandTypePlan::Close);
    REQUIRE(shape.imported_subpaths[2].stroke);           // monotone data line
    CHECK(shape.imported_subpaths[2].path[1].type ==
          render_plan::ShapePathCommandTypePlan::CubicTo);
    REQUIRE(shape.imported_subpaths[3].stroke);           // axes and ticks

    auto changed = source;
    changed["layers"][0]["shape"]["y_ticks"] = 7;
    const auto changed_plan = render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(changed_plan->content_fingerprint != decoded->content_fingerprint);
    auto invalid = source;
    invalid["layers"][0]["shape"]["x_domain"] = {1.0, 9.0};
    CHECK_FALSE(render_plan::decode_render_plan(invalid));
}
