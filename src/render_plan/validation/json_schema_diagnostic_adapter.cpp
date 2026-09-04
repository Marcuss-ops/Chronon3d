#include "json_schema_diagnostic_adapter.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
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
std::string child_path(const std::string& parent, std::string_view key) {
    if (parent == "<root>") return std::string(key);
    return parent + "." + std::string(key);
}
std::string index_path(const std::string& parent, std::size_t index) {
    const auto suffix = "[" + std::to_string(index) + "]";
    return parent == "<root>" ? suffix : parent + suffix;
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
const nlohmann::json& resolve_schema(const nlohmann::json& schema,
                                     const nlohmann::json& root) {
    if (!schema.contains("$ref") || !schema.at("$ref").is_string()) return schema;
    const auto ref = schema.at("$ref").get<std::string>();
    if (ref.rfind("#/", 0) != 0) return schema;
    try {
        return root.at(nlohmann::json::json_pointer(ref.substr(1)));
    } catch (...) {
        return schema;
    }
}
bool matches_type(const nlohmann::json& value, std::string_view type) {
    if (type == "object") return value.is_object();
    if (type == "array") return value.is_array();
    if (type == "integer") return value.is_number_integer() || value.is_number_unsigned();
    if (type == "number") return value.is_number();
    if (type == "string") return value.is_string();
    if (type == "boolean") return value.is_boolean();
    if (type == "null") return value.is_null();
    return true;
}
bool condition_matches(const nlohmann::json& raw_schema,
                       const nlohmann::json& value,
                       const nlohmann::json& root) {
    const auto& schema = resolve_schema(raw_schema, root);
    if (schema.contains("type") && schema.at("type").is_string()
        && !matches_type(value, schema.at("type").get<std::string>()))
        return false;
    if (schema.contains("const") && value != schema.at("const")) return false;
    if (schema.contains("enum")) {
        const auto& values = schema.at("enum");
        if (std::find(values.begin(), values.end(), value) == values.end()) return false;
    }
    if (schema.contains("required")) {
        if (!value.is_object()) return false;
        for (const auto& required : schema.at("required")) {
            if (!value.contains(required.get<std::string>())) return false;
        }
    }
    if (schema.contains("properties")) {
        if (!value.is_object()) return false;
        for (const auto& [key, child_schema] : schema.at("properties").items()) {
            if (value.contains(key)
                && !condition_matches(child_schema, value.at(key), root))
                return false;
        }
    }
    return true;
}
void add_issue(std::vector<ValidationIssue>& issues, std::string path,
               ValidationIssueKind kind, std::string expected,
               const nlohmann::json& actual, std::string detail) {
    const auto duplicate = std::any_of(
        issues.begin(), issues.end(), [&](const ValidationIssue& issue) {
            return issue.path == path && issue.kind == kind;
        });
    if (!duplicate) {
        issues.push_back({std::move(path), kind, std::move(expected),
                          describe_value(actual), std::move(detail)});
    }
}
void walk_schema(const nlohmann::json& raw_schema,
                 const nlohmann::json& value,
                 const nlohmann::json& root,
                 const std::string& path,
                 std::vector<ValidationIssue>& issues) {
    const auto& schema = resolve_schema(raw_schema, root);
    if (schema.contains("type") && schema.at("type").is_string()) {
        const auto expected = schema.at("type").get<std::string>();
        if (!matches_type(value, expected)) {
            add_issue(issues, path, ValidationIssueKind::WrongType, expected, value,
                      "schema keyword: type");
            return;
        }
    }
    if (schema.contains("const") && value != schema.at("const")) {
        add_issue(issues, path, ValidationIssueKind::ConstMismatch,
                  schema.at("const").dump(), value, "schema keyword: const");
    }
    if (schema.contains("enum")) {
        const auto& allowed = schema.at("enum");
        if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
            add_issue(issues, path, ValidationIssueKind::EnumMismatch,
                      allowed.dump(), value, "schema keyword: enum");
        }
    }
    if (value.is_number()) {
        const auto number = value.get<double>();
        if (schema.contains("minimum")
            && number < schema.at("minimum").get<double>()) {
            add_issue(issues, path, ValidationIssueKind::BelowMinimum,
                      ">= " + schema.at("minimum").dump(), value,
                      "schema keyword: minimum");
        }
        if (schema.contains("exclusiveMinimum")
            && number <= schema.at("exclusiveMinimum").get<double>()) {
            add_issue(issues, path, ValidationIssueKind::BelowMinimum,
                      "> " + schema.at("exclusiveMinimum").dump(), value,
                      "schema keyword: exclusiveMinimum");
        }
        if (schema.contains("maximum")
            && number > schema.at("maximum").get<double>()) {
            add_issue(issues, path, ValidationIssueKind::AboveMaximum,
                      "<= " + schema.at("maximum").dump(), value,
                      "schema keyword: maximum");
        }
    }
    if (value.is_string() && schema.contains("minLength")
        && value.get_ref<const std::string&>().size()
               < schema.at("minLength").get<std::size_t>()) {
        add_issue(issues, path, ValidationIssueKind::StringTooShort,
                  "length >= " + schema.at("minLength").dump(), value,
                  "schema keyword: minLength");
    }
    if (value.is_array()) {
        if (schema.contains("minItems")
            && value.size() < schema.at("minItems").get<std::size_t>()) {
            add_issue(issues, path, ValidationIssueKind::ArrayTooShort,
                      "items >= " + schema.at("minItems").dump(), value,
                      "schema keyword: minItems");
        }
        if (schema.contains("maxItems")
            && value.size() > schema.at("maxItems").get<std::size_t>()) {
            add_issue(issues, path, ValidationIssueKind::ArrayTooLong,
                      "items <= " + schema.at("maxItems").dump(), value,
                      "schema keyword: maxItems");
        }
        if (schema.contains("items")) {
            for (std::size_t i = 0; i < value.size(); ++i)
                walk_schema(schema.at("items"), value.at(i), root,
                            index_path(path, i), issues);
        }
    }
    if (value.is_object()) {
        if (schema.contains("required")) {
            for (const auto& required : schema.at("required")) {
                const auto key = required.get<std::string>();
                if (!value.contains(key)) {
                    add_issue(issues, child_path(path, key),
                              ValidationIssueKind::MissingField,
                              "required property", nlohmann::json(nullptr),
                              "schema keyword: required");
                }
            }
        }
        if (schema.value("additionalProperties", true) == false
            && schema.contains("properties")) {
            const auto& properties = schema.at("properties");
            for (const auto& [key, child] : value.items()) {
                (void)child;
                if (!properties.contains(key)) {
                    add_issue(issues, child_path(path, key),
                              ValidationIssueKind::UnknownField,
                              "declared property", value.at(key),
                              "schema keyword: additionalProperties");
                }
            }
        }
        if (schema.contains("properties")) {
            for (const auto& [key, child_schema] : schema.at("properties").items()) {
                if (value.contains(key))
                    walk_schema(child_schema, value.at(key), root,
                                child_path(path, key), issues);
            }
        }
    }
    if (schema.contains("oneOf")) {
        const auto& choices = schema.at("oneOf");
        const auto it = std::find_if(choices.begin(), choices.end(),
            [&](const nlohmann::json& choice) {
                return condition_matches(choice, value, root);
            });
        if (it != choices.end()) {
            walk_schema(*it, value, root, path, issues);
        } else {
            add_issue(issues, path, ValidationIssueKind::WrongType,
                      "oneOf schema", value, "schema keyword: oneOf");
        }
    }
    if (schema.contains("allOf")) {
        for (const auto& clause : schema.at("allOf")) {
            if (clause.contains("if")) {
                if (condition_matches(clause.at("if"), value, root)
                    && clause.contains("then"))
                    walk_schema(clause.at("then"), value, root, path, issues);
                else if (!condition_matches(clause.at("if"), value, root)
                         && clause.contains("else"))
                    walk_schema(clause.at("else"), value, root, path, issues);
            } else {
                walk_schema(clause, value, root, path, issues);
            }
        }
    }
}
const nlohmann::json& instance_at_pointer(const nlohmann::json& instance,
                                          std::string_view pointer) {
    if (pointer.empty()) return instance;
    try {
        return instance.at(nlohmann::json::json_pointer(std::string(pointer)));
    } catch (...) {
        return instance;
    }
}
}  // namespace
std::vector<ValidationIssue> adapt_json_schema_diagnostics(
    const nlohmann::json& schema,
    const nlohmann::json& instance,
    const std::vector<RawSchemaError>& raw_errors) {
    std::vector<ValidationIssue> issues;
    if (raw_errors.empty()) return issues;
    walk_schema(schema, instance, schema, "<root>", issues);
    if (!issues.empty()) return issues;
    const auto& raw = raw_errors.front();
    const auto& actual = instance_at_pointer(instance, raw.pointer);
    issues.push_back({path_from_pointer(raw.pointer),
                      ValidationIssueKind::UnsupportedKeyword,
                      "schema constraint", describe_value(actual), raw.detail});
    return issues;
}
}  // namespace chronon3d::render_plan::validation
