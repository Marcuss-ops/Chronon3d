// Structural render-plan validation backed by the canonical JSON Schema.

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include "generated_render_plan_schema.hpp"
#include "validation/json_schema_diagnostic_adapter.hpp"

#include <nlohmann/json-schema.hpp>

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::render_plan {
namespace {

struct ErrorHandler final : nlohmann::json_schema::basic_error_handler {
    std::vector<validation::RawSchemaError> errors;

    void error(const nlohmann::json::json_pointer& pointer,
               const nlohmann::json& instance,
               const std::string& detail) override {
        nlohmann::json_schema::basic_error_handler::error(pointer, instance, detail);
        errors.push_back({pointer.to_string(), detail});
    }
};

}  // namespace

std::string ValidationIssue::to_string() const {
    std::ostringstream os;
    os << path << ": ";
    switch (kind) {
        case ValidationIssueKind::MissingField: os << "missing field"; break;
        case ValidationIssueKind::UnknownField: os << "unknown field"; break;
        case ValidationIssueKind::WrongType: os << "wrong type"; break;
        case ValidationIssueKind::ConstMismatch: os << "const mismatch"; break;
        case ValidationIssueKind::EnumMismatch: os << "enum mismatch"; break;
        case ValidationIssueKind::BelowMinimum: os << "below minimum"; break;
        case ValidationIssueKind::AboveMaximum: os << "above maximum"; break;
        case ValidationIssueKind::StringTooShort: os << "string too short"; break;
        case ValidationIssueKind::ArrayTooShort: os << "array too short"; break;
        case ValidationIssueKind::ArrayTooLong: os << "array too long"; break;
        case ValidationIssueKind::UnsupportedKeyword: os << "unsupported keyword"; break;
        case ValidationIssueKind::SchemaViolation: os << "schema violation"; break;
    }
    os << " (expected " << expected << ", got " << actual << ")";
    if (!detail.empty()) os << " [" << detail << "]";
    return os.str();
}

std::string ValidationResult::format() const {
    if (issues.empty()) return "render_plan: validation passed (0 issues)";
    std::ostringstream os;
    os << "render_plan: validation failed (" << issues.size() << " issue(s)):\n";
    for (const auto& issue : issues) os << "  - " << issue.to_string() << "\n";
    return os.str();
}

ValidationResult validate_render_plan(const nlohmann::json& root) {
    ValidationResult result;
    try {
        const auto& schema = render_plan_schema();
        nlohmann::json_schema::json_validator validator(schema);
        ErrorHandler handler;
        validator.validate(root, handler);
        result.issues = validation::adapt_json_schema_diagnostics(root, handler.errors);
    } catch (const std::exception& error) {
        result.issues.push_back({"<root>", ValidationIssueKind::SchemaViolation,
                                 "valid JSON Schema", "validator error", error.what()});
    }
    return result;
}

void validate_render_plan_or_throw(const nlohmann::json& root) {
    const auto result = validate_render_plan(root);
    if (!result.ok()) throw std::runtime_error(result.format());
}

}  // namespace chronon3d::render_plan
