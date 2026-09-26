// RenderPlan JSON-Schema validator contract tests.
// nlohmann-json-schema-validator is the only structural authority; Chronon
// tests validation outcome plus preservation of raw validator diagnostics.

#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using chronon3d::render_plan::validate_render_plan;
using chronon3d::render_plan::validate_render_plan_or_throw;
using chronon3d::render_plan::ValidationIssueKind;

namespace {

json make_minimal_valid_plan() {
    return json{
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {
            {"width", 1920},
            {"height", 1080},
            {"fps_num", 30},
            {"fps_den", 1},
            {"duration_frames", 60}
        }},
        {"layers", json::array({
            {
                {"id", "background"},
                {"type", "color"},
                {"color", json::array({0.0, 0.0, 0.0, 1.0})}
            }
        })},
        {"output", {{"path", "out.png"}, {"format", "png"}}}
    };
}

void check_schema_violation(const chronon3d::render_plan::ValidationResult& result) {
    INFO(result.format());
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.issues.empty());
    for (const auto& issue : result.issues) {
        CHECK(issue.kind == ValidationIssueKind::SchemaViolation);
        CHECK(issue.expected == "JSON Schema constraint");
        CHECK_FALSE(issue.detail.empty());
    }
}

}  // namespace

TEST_CASE("render_plan: minimal valid plan passes") {
    const auto result = validate_render_plan(make_minimal_valid_plan());
    INFO(result.format());
    CHECK(result.ok());
    CHECK(result.issues.empty());
}

TEST_CASE("render_plan: extra root-level field is rejected by schema authority") {
    auto plan = make_minimal_valid_plan();
    plan["totally_extra_field"] = "drift";
    const auto result = validate_render_plan(plan);
    check_schema_violation(result);
    CHECK_THROWS_AS(validate_render_plan_or_throw(plan), std::runtime_error);
}

TEST_CASE("render_plan: extra layer field is rejected by schema authority") {
    auto plan = make_minimal_valid_plan();
    plan["layers"][0]["typo"] = "ignored-no-more";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: missing required field is rejected without local reconstruction") {
    auto plan = make_minimal_valid_plan();
    plan["output"].erase("path");
    const auto result = validate_render_plan(plan);
    check_schema_violation(result);
    CHECK_THROWS_AS(validate_render_plan_or_throw(plan), std::runtime_error);
}

TEST_CASE("render_plan: wrong type is rejected without local keyword classification") {
    auto plan = make_minimal_valid_plan();
    plan["canvas"]["width"] = "abc";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: nested required-field violation remains a validator diagnostic") {
    auto plan = make_minimal_valid_plan();
    plan["layers"][0].erase("id");
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: multiple validator errors are preserved") {
    auto plan = make_minimal_valid_plan();
    plan["canvas"]["width"] = 0;
    plan["canvas"]["fps_num"] = -1;
    plan["layers"][0]["type"] = "bogus";
    plan["output"].erase("path");
    plan["output"]["crf"] = 999;
    const auto result = validate_render_plan(plan);
    check_schema_violation(result);
    CHECK(result.issues.size() >= 2u);
}

TEST_CASE("render_plan: const mismatch remains an opaque schema diagnostic") {
    auto plan = make_minimal_valid_plan();
    plan["schema"] = "wrong.schema";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: minLength violation remains an opaque schema diagnostic") {
    auto plan = make_minimal_valid_plan();
    plan["output"]["path"] = "";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: non-object root is rejected without crashing") {
    const json array_root = json::array({1, 2, 3});
    check_schema_violation(validate_render_plan(array_root));
}

TEST_CASE("render_plan V3 shape fill is accepted as an extension and checked by base projection") {
    auto plan = json{
        {"schema", "chronon.render-plan.v3"}, {"version", 3},
        {"canvas", {{"width", 40}, {"height", 30}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 2}}},
        {"layers", json::array({
            {{"id", "shape"}, {"type", "shape"}, {"size", {20, 10}},
             {"color", {0.1, 0.2, 0.3, 1.0}},
             {"shape", {{"type", "rect"}, {"fill", {0.8, 0.7, 0.6, 1.0}}}}}
        })},
        {"output", {{"path", "out.png"}}}
    };
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK(result.ok());

    // validate_render_plan is a pure validator over const JSON. Its V2
    // projection copies shape.fill into layer.color and erases shape.fill on a
    // private copy only; the producer JSON must remain untouched.
    CHECK(plan["layers"][0]["color"][0] == 0.1);
    CHECK(plan["layers"][0]["shape"].contains("fill"));
}

TEST_CASE("render_plan V3 exposes typed transition parameters and excludes them from frozen V2") {
    auto plan = json{
        {"schema", "chronon.render-plan.v3"}, {"version", 3},
        {"canvas", {{"width", 40}, {"height", 30}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 12}}},
        {"layers", json::array({
            {{"id", "shape"}, {"type", "color"}, {"color", {0.1, 0.2, 0.3, 1.0}},
             {"transition_in", {{"id", "slide"}, {"duration", 0.5},
                {"params", {{"distance", 0.75}}}}}}
        })},
        {"output", {{"path", "out.png"}}}
    };
    CHECK(validate_render_plan(plan).ok());

    plan["layers"][0]["transition_in"]["params"]["distance"] = 101;
    check_schema_violation(validate_render_plan(plan));
    plan["layers"][0]["transition_in"]["params"] = {{"unexpected", 1}};
    check_schema_violation(validate_render_plan(plan));

    plan["schema"] = "chronon.render-plan.v2";
    plan["version"] = 2;
    plan["layers"][0].erase("transition_in");
    plan["layers"][0]["animation"] = {{"tracks", json::array({
        {{"property", "fill_color"}, {"keyframes", json::array({
            {{"frame", 0}, {"value", {1.0, 0.0, 0.0, 1.0}}}
        })}}
    })}};
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan V3 schema constrains LightRays controls and adjustment payloads") {
    auto plan = json{
        {"schema", "chronon.render-plan.v3"}, {"version", 3},
        {"canvas", {{"width", 40}, {"height", 30}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 2}}},
        {"layers", json::array({
            {{"id", "rays"}, {"type", "color"}, {"color", {0, 0, 0, 1}},
             {"effects", json::array({{{"type", "light_rays"}, {"origin", {0.25, 0.75}},
               {"length", 100000}, {"density", 8}, {"color", {1, 1, 1, 1}}, {"decay", 1}}})}}
        })},
        {"output", {{"path", "out.png"}}}
    };
    CHECK(validate_render_plan(plan).ok());
    const std::vector<std::pair<std::string, json>> invalid_rays{
        {"origin", json::array({-0.01, 0.5})}, {"origin", json::array({0.5, 1.01})},
        {"length", 100001}, {"density", 8.01}, {"decay", -0.01},
        {"color", json::array({1, 0, 0, 1.01})}
    };
    for (const auto& [field, value] : invalid_rays) {
        auto invalid = plan;
        invalid["layers"][0]["effects"][0][field] = value;
        INFO("invalid LightRays field: " << field);
        check_schema_violation(validate_render_plan(invalid));
    }
    auto v2_light_rays = plan;
    v2_light_rays["schema"] = "chronon.render-plan.v2";
    v2_light_rays["version"] = 2;
    check_schema_violation(validate_render_plan(v2_light_rays));

    plan["layers"] = json::array({
        {{"id", "adjust"}, {"type", "adjustment"},
         {"effects", json::array({{{"type", "noise"}}})}}
    });
    CHECK(validate_render_plan(plan).ok());
    for (const auto* field : {"screen_space", "enable_3d", "opacity", "blend_mode",
                              "safe_insets", "depth_role", "depth_offset", "radius",
                              "start_frame", "duration_frames", "fit", "loop",
                              "transition_in", "transition_out"}) {
        auto invalid = plan;
        if (std::string(field) == "safe_insets") invalid["layers"][0][field] = json::object();
        else if (std::string(field) == "depth_role" || std::string(field) == "fit" ||
                 std::string(field) == "blend_mode") invalid["layers"][0][field] = "value";
        else if (std::string(field) == "transition_in" || std::string(field) == "transition_out")
            invalid["layers"][0][field] = {{"id", "none"}};
        else invalid["layers"][0][field] = 1;
        INFO("forbidden adjustment field: " << field);
        check_schema_violation(validate_render_plan(invalid));
    }
}

TEST_CASE("render_plan V3 schema rejects malformed path fields and gradient stops") {
    auto plan = json{
        {"schema", "chronon.render-plan.v3"}, {"version", 3},
        {"canvas", {{"width", 40}, {"height", 30}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 2}}},
        {"layers", json::array({
            {{"id", "shape"}, {"type", "shape"}, {"size", {20, 10}},
             {"shape", {{"type", "path"}, {"path", {{{"type", "move_to"}, {"point", {0}}}}}}}}
        })},
        {"output", {{"path", "out.png"}}}
    };
    check_schema_violation(validate_render_plan(plan));

    plan["layers"][0]["shape"] = {
        {"type", "rect"},
        {"fill", {{"type", "linear"},
                  {"color_stops", {{{"position", 0.5}, {"color", {1, 0, 0, 1}}}}}}}
    };
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: validator instance pointers are converted but never inferred") {
    auto plan = make_minimal_valid_plan();
    plan["layers"][0]["animation"] = {
        {"tracks", json::array({
            {{"property", "invalid_property"}, {"keyframes", json::array({
                {{"frame", 0}, {"value", 1.0}}
            })}}
        })}
    };
    const auto result = validate_render_plan(plan);
    check_schema_violation(result);
    bool saw_nested_path = false;
    for (const auto& issue : result.issues) {
        if (issue.path.find("layers[0]") != std::string::npos) {
            saw_nested_path = true;
            break;
        }
    }
    CHECK(saw_nested_path);
}
