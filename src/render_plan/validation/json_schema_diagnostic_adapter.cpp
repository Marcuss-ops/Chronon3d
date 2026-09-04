#include "json_schema_diagnostic_adapter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace chronon3d::render_plan::validation {
namespace {

std::string path_from_pointer(std::string_view raw) {
    if (raw.empty()) return "<root>";
    std::string result;
    std::size_t start = raw.front() == '/' ? 1U : 0U;
    while (start <= raw.size()) {
        const auto end = raw.find('/', start);
        auto token = std::string(raw.substr(
            start, end == std::string_view::npos ? raw.size() - start : end - start));
        std::string decoded;
        for (std::size_t i = 0; i < token.size(); ++i) {
            if (token[i] == '~' && i + 1 < token.size()) {
                const char escaped = token[++i];
                decoded += escaped == '0' ? '~' : escaped == '1' ? '/' : escaped;
            } else {
                decoded += token[i];
            }
        }
        const bool index = !decoded.empty() && std::all_of(
            decoded.begin(), decoded.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; });
        if (index) {
            result += '[' + decoded + ']';
        } else {
            if (!result.empty()) result += '.';
            result += decoded;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result.empty() ? "<root>" : result;
}

std::string describe_value(const nlohmann::json& value) {
    if (value.is_string()) return "string \"" + value.get<std::string>() + "\"";
    if (value.is_number_integer())
        return "integer " + std::to_string(value.get<std::int64_t>());
    if (value.is_number_unsigned())
        return "unsigned " + std::to_string(value.get<std::uint64_t>());
    if (value.is_number_float())
        return "number " + std::to_string(value.get<double>());
    if (value.is_boolean())
        return std::string("boolean ") + (value.get<bool>() ? "true" : "false");
    if (value.is_null()) return "null";
    if (value.is_array()) return "array(size=" + std::to_string(value.size()) + ")";
    if (value.is_object()) return "object(keys=" + std::to_string(value.size()) + ")";
    return "<unknown>";
}

const nlohmann::json& instance_at_pointer(const nlohmann::json& instance,
                                          std::string_view pointer) {
    if (pointer.empty()) return instance;
    try {
        return instance.at(nlohmann::json::json_pointer(std::string(pointer)));
    } catch (...) {
        // The validator is authoritative for the pointer. If a pointer names
        // a missing value (for example a required-property failure), keep the
        // diagnostic intact and fall back only for display of `actual`.
        return instance;
    }
}

}  // namespace

std::vector<ValidationIssue> adapt_json_schema_diagnostics(
    const nlohmann::json& instance,
    const std::vector<RawSchemaError>& raw_errors) {
    std::vector<ValidationIssue> issues;
    issues.reserve(raw_errors.size());

    for (const auto& raw : raw_errors) {
        const auto& actual = instance_at_pointer(instance, raw.pointer);
        issues.push_back({path_from_pointer(raw.pointer),
                          ValidationIssueKind::SchemaViolation,
                          "JSON Schema constraint",
                          describe_value(actual),
                          raw.detail});
    }
    return issues;
}

}  // namespace chronon3d::render_plan::validation
