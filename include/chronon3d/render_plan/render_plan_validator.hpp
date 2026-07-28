// ═══════════════════════════════════════════════════════════════════════════
// render_plan/render_plan_validator.hpp — JSON Schema validator for
// `chronon.render-plan` v1 plans.
//
// Hand-rolled subset of JSON Schema Draft 2020-12.  Intentionally NOT a
// full Draft 2020-12 implementation: only the keywords actually used by
// `schemas/chronon.render-plan.v1.schema.json` are supported, so the
// implementation stays tiny and grep-discoverable.  When the schema
// evolves to use a keyword outside this subset, the FAIL-FAST
// `validate_subschema` guard will emit a `GATE_FAIL`-style issue so the
// next maintainer knows to extend the validator.
//
// Supported keywords (Draft 2020-12):
//   - `type`  (object | array | integer | number | string | boolean | null)
//   - `const` / `enum`
//   - `required` (object-level list of property names)
//   - `additionalProperties` (true | false — sealed)
//   - `minLength`, `minimum`, `exclusiveMinimum`, `maximum`
//   - `minItems`, `maxItems`
//   - `properties` (nested), `items` (array element schema)
//
// Reference: schemas/chronon.render-plan.v1.schema.json
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
    UnsupportedKeyword, ///< schema uses a keyword this validator subset
                        ///< doesn't implement (fail-loud extension hook)
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