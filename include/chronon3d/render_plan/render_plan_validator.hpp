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
// fail-fast mapping while `validate_render_plan` returns diagnostics emitted
// by the validator without re-validating or re-interpreting the schema.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d {
namespace render_plan {

enum class ValidationIssueKind : std::uint8_t {
    MissingField,       ///< legacy category retained for source compatibility
    UnknownField,       ///< legacy category retained for source compatibility
    WrongType,          ///< legacy category retained for source compatibility
    ConstMismatch,      ///< legacy category retained for source compatibility
    EnumMismatch,       ///< legacy category retained for source compatibility
    BelowMinimum,       ///< legacy category retained for source compatibility
    AboveMaximum,       ///< legacy category retained for source compatibility
    StringTooShort,     ///< legacy category retained for source compatibility
    ArrayTooShort,      ///< legacy category retained for source compatibility
    ArrayTooLong,       ///< legacy category retained for source compatibility
    UnsupportedKeyword, ///< legacy category retained for source compatibility
    SchemaViolation,    ///< raw json-schema-validator diagnostic; no local revalidation
};

struct ValidationIssue {
    /// Instance path reported by the validator, rendered for humans.
    std::string path;
    ValidationIssueKind kind{ValidationIssueKind::SchemaViolation};
    /// Stable generic expectation. Schema keywords are not reconstructed locally.
    std::string expected;
    /// Human-readable description of the instance value at `path` when available.
    std::string actual;
    /// Original validator diagnostic, preserved verbatim and never parsed.
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
/// nlohmann-json-schema-validator is the only structural-validation authority.
/// Chronon's adapter performs diagnostic conversion only: validator instance
/// pointer + original detail -> ValidationIssue. It never walks the schema,
/// evaluates schema conditions, or infers a keyword/category from message text.
ValidationResult validate_render_plan(const nlohmann::json& root);

/// Throw std::runtime_error when validation produces any issue.
void validate_render_plan_or_throw(const nlohmann::json& root);

}  // namespace render_plan
}  // namespace chronon3d
