// ═══════════════════════════════════════════════════════════════════════════
// tests/c_abi/test_render_plan_validator.cpp
//
// 4 TEST_CASE per TICKET-JSON-SCHEMA-VALIDATOR spec:
//   1. Plan valido minimo → ok
//   2. Plan con campo extra (root-level unknown) → respinto
//   3. Plan senza campo required (output.path) → respinto
//   4. Plan con tipo sbagliato (canvas.width = "abc") → respinto
//
// Plus 2 support SUBCASEs:
//   - Nested required-field violation (layers[].id missing)
//   - Multiple issues accumulated in a single pass (no fail-fast)
//
// The validator is pure (no SDK state, no asset access, no logging),
// so these tests are deterministic and DO NOT require any rendering
// backend, font, or asset.
//
// Test main entry point: `tests/test_main.cpp` (added by
// chronon3d_add_test_suite as TEST_MAIN).  The Chronon3D test
// hygiene invariant (tools/check_test_hygiene.sh Check 1) requires
// that ONLY `tests/test_main.cpp` defines the doctest main entry —
// this file must NOT define its own doctest main macro.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>
#include <stdexcept>

using nlohmann::json;
using chronon3d::render_plan::validate_render_plan;
using chronon3d::render_plan::validate_render_plan_or_throw;
using chronon3d::render_plan::ValidationResult;
using chronon3d::render_plan::ValidationIssueKind;

namespace {

// Minimal valid plan per `schemas/chronon.render-plan.v1.schema.json`.
// All required fields (schema, version, canvas, layers, output) present,
// each with all sub-required fields; one optional `color` layer as a
// smoke-test that nested property walking does not produce spurious
// issues.
json make_minimal_valid_plan() {
    return json{
        {"schema", "chronon.render-plan"},
        {"version", 1},
        {"canvas", {
            {"width", 1920},
            {"height", 1080},
            {"fps", 30},
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

}  // namespace

TEST_CASE("render_plan: minimal valid plan passes") {
    const auto plan = make_minimal_valid_plan();
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK(result.ok());
    CHECK(result.issues.empty());
}

TEST_CASE("render_plan: extra root-level field rejected (additionalProperties:false)") {
    auto plan = make_minimal_valid_plan();
    plan["totally_extra_field"] = "drift";
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    REQUIRE_EQ(result.issues.size(), 1u);
    CHECK(result.issues[0].path == "totally_extra_field");
    CHECK(result.issues[0].kind == ValidationIssueKind::UnknownField);

    // The throw overload should also surface this.
    CHECK_THROWS_AS(validate_render_plan_or_throw(plan), std::runtime_error);
}

TEST_CASE("render_plan: extra layer field rejected (additionalProperties:false)") {
    auto plan = make_minimal_valid_plan();
    plan["layers"][0]["typo"] = "ignored-no-more";
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    CHECK(result.issues[0].path == "layers[0].typo");
    CHECK(result.issues[0].kind == ValidationIssueKind::UnknownField);
}

TEST_CASE("render_plan: missing required field rejected (output.path)") {
    // SUBCASE 3 in the spec — interpret "missing codec" as "missing a
    // required field".  `codec` is OPTIONAL in the schema; pick a
    // required field (`output.path`) for the test.  The interpretation
    // is documented in the ticket cronaca home.
    auto plan = make_minimal_valid_plan();
    plan["output"].erase("path");
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    REQUIRE_EQ(result.issues.size(), 1u);
    CHECK(result.issues[0].path == "output.path");
    CHECK(result.issues[0].kind == ValidationIssueKind::MissingField);

    CHECK_THROWS_AS(validate_render_plan_or_throw(plan), std::runtime_error);
}

TEST_CASE("render_plan: wrong type rejected (canvas.width is string, not integer)") {
    auto plan = make_minimal_valid_plan();
    plan["canvas"]["width"] = "abc";
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    REQUIRE_EQ(result.issues.size(), 1u);
    CHECK(result.issues[0].path == "canvas.width");
    CHECK(result.issues[0].kind == ValidationIssueKind::WrongType);
    CHECK(result.issues[0].expected == "integer");

    CHECK_THROWS_AS(validate_render_plan_or_throw(plan), std::runtime_error);
}

// ── Additional diagnostic SUBCASEs to lock the validator semantics ─────

TEST_CASE("render_plan: nested required-field violation surfaces at correct path") {
    auto plan = make_minimal_valid_plan();
    plan["layers"][0].erase("id");  // `id` is required on layer items
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    REQUIRE_EQ(result.issues.size(), 1u);
    CHECK(result.issues[0].path == "layers[0].id");
    CHECK(result.issues[0].kind == ValidationIssueKind::MissingField);
}

TEST_CASE("render_plan: multiple issues accumulated in one pass (no fail-fast)") {
    auto plan = make_minimal_valid_plan();
    plan["canvas"]["width"] = 0;        // below minimum (minimum: 1)
    plan["canvas"]["fps"] = -1;         // below minimum
    plan["layers"][0]["type"] = "bogus";// enum mismatch
    plan["output"].erase("path");       // missing required
    plan["output"]["crf"] = 999;        // above maximum (maximum: 63)
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    // We expect at least 5 distinct issues (one per violation).
    CHECK(result.issues.size() >= 5u);

    // Spot-check that each category is represented.
    bool saw_below_min = false, saw_enum = false,
         saw_missing = false, saw_above_max = false;
    for (const auto& issue : result.issues) {
        if (issue.kind == ValidationIssueKind::BelowMinimum)  saw_below_min = true;
        if (issue.kind == ValidationIssueKind::EnumMismatch)  saw_enum = true;
        if (issue.kind == ValidationIssueKind::MissingField)  saw_missing = true;
        if (issue.kind == ValidationIssueKind::AboveMaximum)  saw_above_max = true;
    }
    CHECK(saw_below_min);
    CHECK(saw_enum);
    CHECK(saw_missing);
    CHECK(saw_above_max);
}

TEST_CASE("render_plan: const mismatch on schema/version surfaces as WrongType/ConstMismatch") {
    // `schema` is `{"const": "chronon.render-plan"}` — mismatch should
    // emit ConstMismatch, NOT WrongType (since the schema has no `type`).
    auto plan = make_minimal_valid_plan();
    plan["schema"] = "wrong.schema";
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    REQUIRE_EQ(result.issues.size(), 1u);
    CHECK(result.issues[0].path == "schema");
    CHECK(result.issues[0].kind == ValidationIssueKind::ConstMismatch);
}

TEST_CASE("render_plan: minLength violation on output.path surfaces as StringTooShort") {
    auto plan = make_minimal_valid_plan();
    plan["output"]["path"] = "";
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    REQUIRE_EQ(result.issues.size(), 1u);
    CHECK(result.issues[0].path == "output.path");
    CHECK(result.issues[0].kind == ValidationIssueKind::StringTooShort);
}

TEST_CASE("render_plan: non-object root emits WrongType without crashing") {
    const json array_root = json::array({1, 2, 3});
    const auto result = validate_render_plan(array_root);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    REQUIRE_EQ(result.issues.size(), 1u);
    CHECK(result.issues[0].path == "<root>");
    CHECK(result.issues[0].kind == ValidationIssueKind::WrongType);
}

// Minor 1 from code-review: lock the path builder for nested-in-animation
// (object-in-object-in-array) — the deepest path nesting actually exercised
// by the canonical schema.  Without this SUBCASE, a regression in the path
// builder that only manifested at depth-3 would silently rot.
TEST_CASE("render_plan: nested array > object > object path built correctly") {
    auto plan = make_minimal_valid_plan();
    // Force an animation block on layer[0] so the path is layers[0].animation.X
    plan["layers"][0]["animation"] = {
        {"preset", ""}  // empty preset → minLength violation at layers[0].animation.preset
    };
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK_FALSE(result.ok());
    // At minimum: layers[0].animation.preset has StringTooShort.
    bool found_path = false;
    for (const auto& issue : result.issues) {
        if (issue.path == "layers[0].animation.preset"
            && issue.kind == ValidationIssueKind::StringTooShort) {
            found_path = true;
            break;
        }
    }
    CHECK(found_path);
}
