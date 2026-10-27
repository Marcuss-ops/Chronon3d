#include <chronon3d/render_plan/easing_contract.hpp>
// Structural render-plan validation backed by the canonical JSON Schemas.

#include <chronon3d/render_plan/render_plan_validator.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include "generated_render_plan_schema.hpp"
#include "render_plan_validator_detail.hpp"
#include "validation/json_schema_diagnostic_adapter.hpp"

#include <nlohmann/json-schema.hpp>

#include <sstream>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
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

std::vector<validation::RawSchemaError> validate_against(
    const nlohmann::json& instance, const nlohmann::json& schema) {
    const nlohmann::json_schema::schema_loader loader =
        [](const nlohmann::json_uri& id, nlohmann::json& loaded) {
            if (id.path().ends_with("chronon.render-plan.v2.schema.json")) {
                loaded = render_plan_v2_schema();
                return;
            }
            throw std::runtime_error("unsupported embedded render-plan schema reference: " +
                                     id.location());
        };
    nlohmann::json_schema::json_validator validator(schema, loader);
    ErrorHandler handler;
    validator.validate(instance, handler);
    return std::move(handler.errors);
}


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
        std::vector<validation::RawSchemaError> errors;
        const auto schema_id = root.is_object()
            ? root.value("schema", std::string{}) : std::string{};
        if (schema_id == kRenderPlanSchemaV3) {
            errors = validate_against(root, render_plan_v3_extensions_schema());
            auto base_errors = validate_against(
                validation_detail::v2_projection(root), render_plan_v2_schema());
            errors.insert(errors.end(),
                          std::make_move_iterator(base_errors.begin()),
                          std::make_move_iterator(base_errors.end()));
        } else {
            errors = validate_against(root, render_plan_v2_schema());
        }
        result.issues = validation::adapt_json_schema_diagnostics(root, errors);
    } catch (const std::exception& error) {
        result.issues.push_back(make_validation_issue(
            "<root>", ValidationIssueKind::SchemaViolation,
            "valid JSON Schema", "validator error", error.what()));
    }
    return result;
}

void validate_render_plan_or_throw(const nlohmann::json& root) {
    const auto result = validate_render_plan(root);
    if (!result.ok()) throw std::runtime_error(result.format());
}

}  // namespace chronon3d::render_plan
