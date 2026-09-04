#pragma once

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace chronon3d::render_plan::validation {

/// Raw diagnostic emitted by nlohmann-json-schema-validator.
/// `detail` is opaque to Chronon: adapters must preserve it, never parse it.
struct RawSchemaError {
    std::string pointer;
    std::string detail;
};

/// Convert validator diagnostics to Chronon's stable envelope without
/// inspecting or re-evaluating the JSON Schema.
std::vector<ValidationIssue> adapt_json_schema_diagnostics(
    const nlohmann::json& instance,
    const std::vector<RawSchemaError>& raw_errors);

}  // namespace chronon3d::render_plan::validation
