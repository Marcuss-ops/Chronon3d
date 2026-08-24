// Structural render-plan validation backed by the canonical JSON Schema.

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include "generated_render_plan_schema.hpp"

#include <nlohmann/json-schema.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

namespace chronon3d::render_plan {
namespace {

std::string path_from_pointer(const nlohmann::json::json_pointer& pointer) {
    const auto raw = pointer.to_string();
    if (raw.empty()) return "<root>";
    std::string result;
    std::size_t start = 1;
    while (start <= raw.size()) {
        const auto end = raw.find('/', start);
        auto token = raw.substr(start, end == std::string::npos
                                           ? std::string::npos : end - start);
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
        if (index) result += '[' + decoded + ']';
        else {
            if (!result.empty()) result += '.';
            result += decoded;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

ValidationIssueKind classify(const std::string& detail) {
    const auto message = lower(detail);
    if (message.find("required") != std::string::npos
        || message.find("missing") != std::string::npos) return ValidationIssueKind::MissingField;
    if (message.find("additional") != std::string::npos
        || message.find("unknown") != std::string::npos) return ValidationIssueKind::UnknownField;
    if (message.find("const") != std::string::npos) return ValidationIssueKind::ConstMismatch;
    if (message.find("enum") != std::string::npos
        || message.find("one of") != std::string::npos) return ValidationIssueKind::EnumMismatch;
    if (message.find("minimum") != std::string::npos
        || message.find("exclusiveminimum") != std::string::npos) return ValidationIssueKind::BelowMinimum;
    if (message.find("maximum") != std::string::npos) return ValidationIssueKind::AboveMaximum;
    if (message.find("minlength") != std::string::npos) return ValidationIssueKind::StringTooShort;
    if (message.find("minitems") != std::string::npos) return ValidationIssueKind::ArrayTooShort;
    if (message.find("maxitems") != std::string::npos) return ValidationIssueKind::ArrayTooLong;
    return ValidationIssueKind::WrongType;
}

std::string expected_from_detail(const std::string& detail) {
    for (const auto type : {"object", "array", "integer", "number", "string", "boolean", "null"}) {
        if (detail.find(type) != std::string::npos) return type;
    }
    return detail;
}

const nlohmann::json* schema_at_instance(const nlohmann::json& schema,
                                         const nlohmann::json::json_pointer& pointer) {
    const auto raw = pointer.to_string();
    const nlohmann::json* current = &schema;
    std::size_t start = raw.empty() ? raw.size() : 1;
    while (start < raw.size()) {
        const auto end = raw.find('/', start);
        const auto token = raw.substr(start, end == std::string::npos
                                               ? std::string::npos : end - start);
        const bool index = !token.empty()
            && std::all_of(token.begin(), token.end(), [](unsigned char c) {
                return std::isdigit(c) != 0;
            });
        if (index) {
            if (!current->contains("items")) return nullptr;
            current = &current->at("items");
        } else {
            if (!current->contains("properties")
                || !current->at("properties").contains(token)) return nullptr;
            current = &current->at("properties").at(token);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return current;
}

std::string describe_value(const nlohmann::json& value) {
    if (value.is_string()) return "string \"" + value.get<std::string>() + "\"";
    if (value.is_number_integer()) return "integer " + std::to_string(value.get<std::int64_t>());
    if (value.is_number_unsigned()) return "unsigned " + std::to_string(value.get<std::uint64_t>());
    if (value.is_number_float()) return "number " + std::to_string(value.get<double>());
    if (value.is_boolean()) return std::string("boolean ") + (value.get<bool>() ? "true" : "false");
    if (value.is_null()) return "null";
    if (value.is_array()) return "array(size=" + std::to_string(value.size()) + ")";
    if (value.is_object()) return "object(keys=" + std::to_string(value.size()) + ")";
    return "<unknown>";
}

struct ErrorHandler final : nlohmann::json_schema::basic_error_handler {
    std::vector<ValidationIssue> issues;
    const nlohmann::json& schema;

    explicit ErrorHandler(const nlohmann::json& schema_ref) : schema(schema_ref) {}

    void error(const nlohmann::json::json_pointer& pointer,
               const nlohmann::json& instance,
               const std::string& detail) override {
        nlohmann::json_schema::basic_error_handler::error(pointer, instance, detail);
        const auto message = lower(detail);
        const auto kind = message.find("required enum") != std::string::npos
            ? ValidationIssueKind::EnumMismatch : classify(detail);
        auto path = path_from_pointer(pointer);
        // The library reports a missing required property at its parent.
        if (kind == ValidationIssueKind::MissingField
            || kind == ValidationIssueKind::UnknownField) {
            const auto quote = detail.find('\'');
            if (quote != std::string::npos) {
                const auto end = detail.find('\'', quote + 1);
                if (end != std::string::npos) {
                    const auto field = detail.substr(quote + 1, end - quote - 1);
                    path = path == "<root>" ? field : path + "." + field;
                }
            }
        }
        auto expected = expected_from_detail(detail);
        if (expected == "unexpected instance type") {
            if (const auto* node = schema_at_instance(schema, pointer);
                node && node->contains("type")) {
                expected = node->at("type").get<std::string>();
            }
        }
        if (kind == ValidationIssueKind::MissingField) expected = "required property";
        issues.push_back({std::move(path), kind, std::move(expected),
                          describe_value(instance), detail});
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
        nlohmann::json_schema::json_validator validator(render_plan_schema());
        ErrorHandler handler(render_plan_schema());
        validator.validate(root, handler);
        result.issues = std::move(handler.issues);
    } catch (const std::exception& error) {
        result.issues.push_back({"<root>", ValidationIssueKind::UnsupportedKeyword,
                                 "valid JSON Schema", "validator error", error.what()});
    }
    return result;
}

void validate_render_plan_or_throw(const nlohmann::json& root) {
    const auto result = validate_render_plan(root);
    if (!result.ok()) throw std::runtime_error(result.format());
}

}  // namespace chronon3d::render_plan
