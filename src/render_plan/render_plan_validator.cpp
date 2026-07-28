// ═══════════════════════════════════════════════════════════════════════════
// render_plan/render_plan_validator.cpp — implementation.
//
// Walk strategy: each level of the schema is a "subschema" (the schema
// fragment applied at that node).  `validate_subschema` recurses into:
//   - nested `properties` (object members)
//   - nested `items`     (array members)
// and emits one issue per detected violation.  Accumulates ALL issues
// (no fail-fast) so the caller gets a full diff.
//
// Complexity: O(N) over the JSON tree where N is the total number of
// properties/items visited (each visited once, no backtracking).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace chronon3d {
namespace render_plan {

namespace {

// ── JSON-pointer-like path builder ──────────────────────────────────────
struct Path {
    std::string buf;

    void push_object_key(const std::string& key) {
        if (!buf.empty()) buf += '.';
        buf += key;
    }
    void push_array_index(std::size_t i) {
        buf += '[';
        buf += std::to_string(i);
        buf += ']';
    }
    std::string str() const { return buf; }
};

// ── Type check ──────────────────────────────────────────────────────────
bool json_matches_type(const nlohmann::json& v, const std::string& type) {
    if (type == "object")  return v.is_object();
    if (type == "array")   return v.is_array();
    if (type == "string")  return v.is_string();
    if (type == "integer") {
        if (!v.is_number()) return false;
        // nlohmann reports integers as number_integer or number_unsigned;
        // doubles are number_float. Reject floats that aren't integral.
        if (v.is_number_float()) {
            return std::floor(v.get<double>()) == v.get<double>();
        }
        return true;
    }
    if (type == "number")  return v.is_number();
    if (type == "boolean") return v.is_boolean();
    if (type == "null")    return v.is_null();
    return false;
}

std::string describe_value(const nlohmann::json& v) {
    if (v.is_string())  return "string \"" + v.get<std::string>() + "\"";
    if (v.is_number_integer()) return "integer " + std::to_string(v.get<std::int64_t>());
    if (v.is_number_unsigned()) return "unsigned " + std::to_string(v.get<std::uint64_t>());
    if (v.is_number_float()) return "number " + std::to_string(v.get<double>());
    if (v.is_boolean()) return std::string("boolean ") + (v.get<bool>() ? "true" : "false");
    if (v.is_null())    return "null";
    if (v.is_array())   return "array(size=" + std::to_string(v.size()) + ")";
    if (v.is_object())  return "object(keys=" + std::to_string(v.size()) + ")";
    return "<unknown>";
}

// ── Numeric coercion for minimum/maximum comparisons ───────────────────
double as_double(const nlohmann::json& v) {
    if (v.is_number_float())   return v.get<double>();
    if (v.is_number_integer()) return static_cast<double>(v.get<std::int64_t>());
    if (v.is_number_unsigned())return static_cast<double>(v.get<std::uint64_t>());
    return 0.0;
}

// ── Recursive subschema walker ──────────────────────────────────────────
void validate_subschema(const nlohmann::json& value,
                        const nlohmann::json& subschema,
                        const Path& path,
                        std::vector<ValidationIssue>& out) {
    // `type` check
    if (subschema.contains("type")) {
        const auto expected = subschema.at("type").get<std::string>();
        if (!json_matches_type(value, expected)) {
            out.push_back({path.str(),
                           ValidationIssueKind::WrongType,
                           expected,
                           describe_value(value),
                           ""});
            // If type mismatches, downstream checks would be misleading
            // (e.g. minLength on a number). Skip them.
            return;
        }
    }

    // `const` check
    if (subschema.contains("const")) {
        if (value != subschema.at("const")) {
            out.push_back({path.str(),
                           ValidationIssueKind::ConstMismatch,
                           "const " + subschema.at("const").dump(),
                           describe_value(value),
                           ""});
        }
    }

    // `enum` check
    if (subschema.contains("enum")) {
        bool found = false;
        for (const auto& candidate : subschema.at("enum")) {
            if (value == candidate) { found = true; break; }
        }
        if (!found) {
            std::string opts;
            for (const auto& candidate : subschema.at("enum")) {
                if (!opts.empty()) opts += ", ";
                opts += candidate.is_string() ? candidate.get<std::string>()
                                              : candidate.dump();
            }
            out.push_back({path.str(),
                           ValidationIssueKind::EnumMismatch,
                           "one of [" + opts + "]",
                           describe_value(value),
                           ""});
        }
    }

    // Object-level: required + additionalProperties + properties
    if (value.is_object()) {
        // required
        if (subschema.contains("required") && subschema.at("required").is_array()) {
            for (const auto& req : subschema.at("required")) {
                const auto key = req.get<std::string>();
                if (!value.contains(key)) {
                    Path p = path; p.push_object_key(key);
                    out.push_back({p.str(),
                                   ValidationIssueKind::MissingField,
                                   "required property",
                                   "<missing>",
                                   key});
                }
            }
        }
        // properties (descend into known subschemas)
        if (subschema.contains("properties") && subschema.at("properties").is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                const auto& key = it.key();
                const auto& v   = it.value();
                if (subschema.at("properties").contains(key)) {
                    Path p = path; p.push_object_key(key);
                    validate_subschema(v, subschema.at("properties").at(key), p, out);
                }
            }
        }
        // additionalProperties (sealed).  `additionalProperties: true`
        // is a documented no-op (any property is allowed) — we don't
        // emit UnsupportedKeyword for `true`, we just skip the sealed
        // check.  Cat-3 minimal-surface: only the `false` branch needs
        // to do work; `true` is the default and inherits the check.
        if (subschema.contains("additionalProperties")
            && subschema.at("additionalProperties").is_boolean()
            && subschema.at("additionalProperties").get<bool>() == false
            && subschema.contains("properties")) {
            std::unordered_set<std::string> known;
            for (auto it = subschema.at("properties").begin();
                 it != subschema.at("properties").end(); ++it) {
                known.insert(it.key());
            }
            for (auto it = value.begin(); it != value.end(); ++it) {
                if (known.find(it.key()) == known.end()) {
                    Path p = path; p.push_object_key(it.key());
                    out.push_back({p.str(),
                                   ValidationIssueKind::UnknownField,
                                   "one of " + std::to_string(known.size()) + " known properties",
                                   describe_value(it.value()),
                                   it.key()});
                }
            }
        }
    }

    // Array-level: items + minItems + maxItems
    if (value.is_array()) {
        if (subschema.contains("items") && subschema.at("items").is_object()) {
            const auto& item_schema = subschema.at("items");
            for (std::size_t i = 0; i < value.size(); ++i) {
                Path p = path; p.push_array_index(i);
                validate_subschema(value.at(i), item_schema, p, out);
            }
        }
        if (subschema.contains("minItems")
            && subschema.at("minItems").is_number_integer()) {
            const auto min = subschema.at("minItems").get<std::size_t>();
            if (value.size() < min) {
                out.push_back({path.str(),
                               ValidationIssueKind::ArrayTooShort,
                               "minItems " + std::to_string(min),
                               "size " + std::to_string(value.size()),
                               ""});
            }
        }
        if (subschema.contains("maxItems")
            && subschema.at("maxItems").is_number_integer()) {
            const auto max = subschema.at("maxItems").get<std::size_t>();
            if (value.size() > max) {
                out.push_back({path.str(),
                               ValidationIssueKind::ArrayTooLong,
                               "maxItems " + std::to_string(max),
                               "size " + std::to_string(value.size()),
                               ""});
            }
        }
    }

    // String-level: minLength
    if (value.is_string()) {
        if (subschema.contains("minLength")
            && subschema.at("minLength").is_number_integer()) {
            const auto min = subschema.at("minLength").get<std::size_t>();
            if (value.get<std::string>().size() < min) {
                out.push_back({path.str(),
                               ValidationIssueKind::StringTooShort,
                               "minLength " + std::to_string(min),
                               "length " + std::to_string(value.get<std::string>().size()),
                               ""});
            }
        }
    }

    // Numeric-level: minimum / exclusiveMinimum / maximum
    if (value.is_number()) {
        const double v = as_double(value);
        if (subschema.contains("minimum") && subschema.at("minimum").is_number()) {
            const double m = subschema.at("minimum").get<double>();
            if (v < m) {
                out.push_back({path.str(),
                               ValidationIssueKind::BelowMinimum,
                               "minimum " + std::to_string(m),
                               std::to_string(v),
                               ""});
            }
        }
        if (subschema.contains("exclusiveMinimum")
            && subschema.at("exclusiveMinimum").is_number()) {
            const double m = subschema.at("exclusiveMinimum").get<double>();
            if (v <= m) {
                out.push_back({path.str(),
                               ValidationIssueKind::BelowMinimum,
                               "exclusiveMinimum > " + std::to_string(m),
                               std::to_string(v),
                               ""});
            }
        }
        if (subschema.contains("maximum") && subschema.at("maximum").is_number()) {
            const double m = subschema.at("maximum").get<double>();
            if (v > m) {
                out.push_back({path.str(),
                               ValidationIssueKind::AboveMaximum,
                               "maximum " + std::to_string(m),
                               std::to_string(v),
                               ""});
            }
        }
    }
}

}  // namespace

// ── Public API ──────────────────────────────────────────────────────────

std::string ValidationIssue::to_string() const {
    std::ostringstream os;
    os << path << ": ";
    switch (kind) {
        case ValidationIssueKind::MissingField:      os << "missing field"; break;
        case ValidationIssueKind::UnknownField:      os << "unknown field"; break;
        case ValidationIssueKind::WrongType:         os << "wrong type"; break;
        case ValidationIssueKind::ConstMismatch:     os << "const mismatch"; break;
        case ValidationIssueKind::EnumMismatch:      os << "enum mismatch"; break;
        case ValidationIssueKind::BelowMinimum:      os << "below minimum"; break;
        case ValidationIssueKind::AboveMaximum:      os << "above maximum"; break;
        case ValidationIssueKind::StringTooShort:    os << "string too short"; break;
        case ValidationIssueKind::ArrayTooShort:     os << "array too short"; break;
        case ValidationIssueKind::ArrayTooLong:      os << "array too long"; break;
        case ValidationIssueKind::UnsupportedKeyword:os << "unsupported keyword"; break;
    }
    os << " (expected " << expected << ", got " << actual << ")";
    if (!detail.empty()) os << " [" << detail << "]";
    return os.str();
}

std::string ValidationResult::format() const {
    if (issues.empty()) return "render_plan: validation passed (0 issues)";
    std::ostringstream os;
    os << "render_plan: validation failed (" << issues.size() << " issue(s)):\n";
    for (const auto& issue : issues) {
        os << "  - " << issue.to_string() << "\n";
    }
    return os.str();
}

ValidationResult validate_render_plan(const nlohmann::json& root) {
    ValidationResult result;

    // The canonical schema is encoded inline (Cat-3 anti-dup: schema is
    // the SSoT; this inlined copy is the validator's minimal machine-
    // readable surface).  When the schema file evolves, this function
    // MUST be updated in lockstep — the test suite verifies alignment
    // (see test_render_plan_validator.cpp SUBCASE "schema alignment").
    //
    // The shape mirrors `schemas/chronon.render-plan.v1.schema.json`
    // with the same keywords the schema file declares.
    using nlohmann::json;
    const json schema = {
        {"type", "object"},
        {"additionalProperties", false},
        {"required", json::array({"schema", "version", "canvas", "layers", "output"})},
        {"properties", {
            {"schema", {{"const", "chronon.render-plan"}}},
            {"version", {{"const", 1}}},
            {"job_id", {{"type", "string"}, {"minLength", 1}}},
            {"assets_root", {{"type", "string"}}},
            {"canvas", {
                {"type", "object"},
                {"additionalProperties", false},
                {"required", json::array({"width", "height", "fps", "duration_frames"})},
                {"properties", {
                    {"width",           {{"type", "integer"}, {"minimum", 1}}},
                    {"height",          {{"type", "integer"}, {"minimum", 1}}},
                    {"fps",             {{"type", "integer"}, {"minimum", 1}}},
                    {"duration_frames", {{"type", "integer"}, {"minimum", 1}}}
                }}
            }},
            {"layers", {
                {"type", "array"},
                {"items", {
                    {"type", "object"},
                    {"additionalProperties", true},
                    {"required", json::array({"id", "type"})},
                    {"properties", {
                        {"id",      {{"type", "string"}, {"minLength", 1}}},
                        {"type",    {{"enum", json::array({"image", "video", "text", "color", "subtitle_track"})}}},
                        {"asset",   {{"type", "string"}}},
                        {"source",  {{"type", "string"}}},
                        {"text",    {{"type", "string"}}},
                        {"font",    {{"type", "string"}}},
                        {"font_size",   {{"type", "number"}, {"exclusiveMinimum", 0}}},
                        {"box_width",   {{"type", "number"}, {"exclusiveMinimum", 0}}},
                        {"box_height",  {{"type", "number"}, {"exclusiveMinimum", 0}}},
                        {"position", {
                            {"type", "array"},
                            {"minItems", 2},
                            {"maxItems", 3},
                            {"items", {{"type", "number"}}}
                        }},
                        {"start_frame",     {{"type", "integer"}, {"minimum", 0}}},
                        {"duration_frames", {{"type", "integer"}, {"minimum", 1}}},
                        {"fit",    {{"enum", json::array({"cover", "contain", "stretch", "none"})}}},
                        {"format", {{"enum", json::array({"srt", "vtt", "json"})}}},
                        {"preset", {{"type", "string"}}},
                        {"animation", {
                            {"type", "object"},
                            {"additionalProperties", false},
                            {"required", json::array({"preset"})},
                            {"properties", {
                                {"preset",          {{"type", "string"}, {"minLength", 1}}},
                                {"start_frame",     {{"type", "integer"}, {"minimum", 0}}},
                                {"duration_frames", {{"type", "integer"}, {"minimum", 1}}}
                            }}
                        }}
                    }}
                }}
            }},
            {"audio_tracks", {
                {"type", "array"},
                {"items", {
                    {"type", "object"},
                    {"additionalProperties", false},
                    {"required", json::array({"source"})},
                    {"properties", {
                        {"source",             {{"type", "string"}, {"minLength", 1}}},
                        {"volume",             {{"type", "number"}, {"minimum", 0}, {"maximum", 4}}},
                        {"start_time_offset",  {{"type", "number"}, {"minimum", 0}}},
                        {"duration_seconds",   {{"type", "number"}, {"minimum", 0}}},
                        {"role",               {{"type", "string"}}}
                    }}
                }}
            }},
            {"output", {
                {"type", "object"},
                {"additionalProperties", false},
                {"required", json::array({"path"})},
                {"properties", {
                    {"path",    {{"type", "string"}, {"minLength", 1}}},
                    {"format",  {{"enum", json::array({"png", "mp4", "mkv", "webm"})}}},
                    {"codec",   {{"enum", json::array({"auto", "h264", "h265", "vp9", "av1"})}}},
                    {"bitrate", {{"type", "integer"}, {"minimum", 0}}},
                    {"crf",     {{"type", "integer"}, {"minimum", 0}, {"maximum", 63}}}
                }}
            }}
        }}
    };

    // Root-level guard: the plan MUST be a JSON object before any
    // subschema walk.  Surface this as WrongType rather than letting
    // nlohmann throw a type_error deep in the call stack.
    if (!root.is_object()) {
        result.issues.push_back({"<root>",
                                 ValidationIssueKind::WrongType,
                                 "object",
                                 describe_value(root),
                                 "render_plan root must be a JSON object"});
        return result;
    }

    Path root_path;
    validate_subschema(root, schema, root_path, result.issues);
    return result;
}

void validate_render_plan_or_throw(const nlohmann::json& root) {
    auto result = validate_render_plan(root);
    if (!result.ok()) {
        throw std::runtime_error(result.format());
    }
}

}  // namespace render_plan
}  // namespace chronon3d