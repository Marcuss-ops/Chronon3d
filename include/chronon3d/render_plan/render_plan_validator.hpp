// ═══════════════════════════════════════════════════════════════════════════
// render_plan/render_plan_validator.hpp — JSON Schema validator for
// `chronon.render-plan` v1 plans.
//
// Structural validation is delegated to nlohmann-json-schema-validator.
// The schema in `schemas/chronon.render-plan.v1.schema.json` is the sole
// source of structural rules; runtime asset/backend checks remain semantic.
//
// Cat-3 contract: no SDK state, no asset access, no logging — pure
// functions that walk the parsed JSON against the schema.  Side effect:
// throws std::runtime_error from the `_or_throw` overload so the C API
// can map it to CHRONON_ERROR_PARSE_FAILED unchanged.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <nlohmann/json.hpp>

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
    UnsupportedKeyword, ///< retained for API compatibility with older callers
};

struct ValidationIssue {
    /// JSON pointer-like path to the offending value, e.g. "layers[2].type"
    std::string path;
    ValidationIssueKind kind{ValidationIssueKind::WrongType};
    /// What the schema required (human-readable, e.g. "integer >= 1")
    std::string expected;
    /// What was actually present (human-readable, e.g. "string \"abc\"")
    std::string actual;
    /// Optional human-readable detail.
    std::string detail;

    std::string to_string() const;
};

struct ValidationResult {
    std::vector<ValidationIssue> issues;

    /// true iff no issues were accumulated.
    bool ok() const noexcept { return issues.empty(); }

    /// Render the full issue list as a multi-line, grep-discoverable
    /// string suitable for direct inclusion in error messages.
    std::string format() const;
};

/// Validate `root` against `schemas/chronon.render-plan.v1.schema.json`.
/// Returns ALL issues found (does not fail-fast) so the caller can show
/// the full diff to the user in a single response.
ValidationResult validate_render_plan(const nlohmann::json& root);

/// Convenience overload that throws std::runtime_error on any issue,
/// formatted via ValidationResult::format().  Used by the C API entry
/// points (`compile_plan`, `render_legacy_json`) where the fail-fast
/// semantics match the existing `compile_plan` throw contract.
void validate_render_plan_or_throw(const nlohmann::json& root);

}  // namespace render_plan
}  // namespace chronon3d
