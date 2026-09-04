// ═══════════════════════════════════════════════════════════════════════════
// render_plan/render_plan_validator.hpp — JSON Schema validator for
// `chronon.render-plan.v2` plans.
//
// Structural validity is delegated to nlohmann-json-schema-validator.
// The schema in `schemas/json/chronon.render-plan.v2.schema.json` is the sole
// source of structural rules; runtime asset/backend checks remain semantic.
//
// Cat-3 contract: no SDK state, no asset access, no logging — pure
// functions over parsed JSON. The `_or_throw` overload preserves the C API
// fail-fast mapping while `validate_render_plan` returns structured diagnostics.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d {
namespace render_plan {

enum class ValidationIssueKind : std::uint8_t {
    MissingField,       ///< required field not present
    UnknownField,       ///< additionalProperties: false rejected this key
    WrongType,          ///< `type` mismatch
    ConstMismatch,      ///< `const` value mismatch
    EnumMismatch,       ///< value not in `enum` set
    BelowMinimum,       ///< numeric < minimum (or <= exclusiveMinimum)
    AboveMaximum,       ///< numeric > maximum
    StringTooShort,     ///< minLength violation
    ArrayTooShort,      ///< minItems violation
    ArrayTooLong,       ///< maxItems violation
    UnsupportedKeyword, ///< validator failed on a constraint not mapped structurally
};

struct ValidationIssue {
    /// JSON pointer-like path to the offending value, e.g. "layers[2].type"
    std::string path;
    ValidationIssueKind kind{ValidationIssueKind::WrongType};
    /// What the schema required (human-readable, e.g. "integer >= 1")
    std::string expected;
    /// What was actually present (human-readable, e.g. "string \"abc\"")
    std::string actual;
    /// Optional human-readable detail. Never parsed to determine `kind`.
    std::string detail;

    std::string to_string() const;
};

struct ValidationResult {
    std::vector<ValidationIssue> issues;

    bool ok() const noexcept { return issues.empty(); }
    std::string format() const;
};

/// Validate `root` against the canonical RenderPlan V2 schema.
///
/// nlohmann-json-schema-validator remains the validity authority. Chronon's
/// diagnostic adapter maps schema structure into stable ValidationIssueKind
/// values and never classifies errors by matching human-readable text.
ValidationResult validate_render_plan(const nlohmann::json& root);

/// Throw std::runtime_error when validation produces any issue.
void validate_render_plan_or_throw(const nlohmann::json& root);

}  // namespace render_plan
}  // namespace chronon3d
