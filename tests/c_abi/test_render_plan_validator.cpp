// RenderPlan JSON-Schema validator contract tests.
// nlohmann-json-schema-validator is the only structural authority; Chronon
// tests validation outcome plus preservation of raw validator diagnostics.

#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>
#include <stdexcept>

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
