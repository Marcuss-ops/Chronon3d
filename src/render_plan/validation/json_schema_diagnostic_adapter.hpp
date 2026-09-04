#pragma once

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace chronon3d::render_plan::validation {

struct RawSchemaError {
    std::string pointer;
    std::string detail;
};

std::vector<ValidationIssue> adapt_json_schema_diagnostics(
    const nlohmann::json& schema,
    const nlohmann::json& instance,
    const std::vector<RawSchemaError>& raw_errors);

}  // namespace chronon3d::render_plan::validation
