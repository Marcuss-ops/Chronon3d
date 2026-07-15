// ═══════════════════════════════════════════════════════════════════════════
// command_schema.cpp — Phase 1d / Increment C
// ───────────────────────────────────────────────────────────────────────────
//
// `chronon schema <comp_id>` — emits the PropsSchema of a registered
// composition as machine-readable JSON.  Distinct from `chronon info`
// (human-readable metadata+props) and from `chronon example-props`
// (default ValueMap).  Wraps canonical
// `CompositionRegistry::descriptor_of(name)` only — no `CompositionResolveFn`
// lambda or any abandoned abstraction.
//
// Exit codes: 0 = PASS, 1 = FAIL (unknown composition).

#include "../../commands.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <iostream>
#include <string>

namespace chronon3d {
namespace cli {

namespace {

const char* prop_type_name(PropType type) {
    switch (type) {
        case PropType::String:  return "string";
        case PropType::Integer: return "integer";
        case PropType::Float:   return "float";
        case PropType::Boolean: return "boolean";
        case PropType::Enum:    return "enum";
        case PropType::Color:   return "color";
        case PropType::Path:    return "path";
    }
    return "unknown";
}

nlohmann::json field_to_json(const PropField& field) {
    nlohmann::json j;
    j["name"]     = field.name;
    j["type"]     = prop_type_name(field.type);
    j["required"] = field.required;
    if (!field.description.empty()) {
        j["description"] = field.description;
    }
    if (field.default_value) {
        j["default"] = *field.default_value;
    }
    if (!field.enum_values.empty()) {
        j["enum_values"] = field.enum_values;
    }
    return j;
}

} // namespace

int command_schema(const CompositionRegistry& registry, const SchemaArgs& args) {
    const auto descriptor = registry.descriptor_of(args.comp_id);
    if (!descriptor) {
        spdlog::error("schema: unknown composition '{}'", args.comp_id);
        nlohmann::json err;
        err["error"]         = "composition_not_found";
        err["composition_id"] = args.comp_id;
        err["status"]        = "FAIL";
        std::cout << err.dump(2) << "\n";
        return 1;
    }

    nlohmann::json js;
    js["composition_id"] = descriptor->id;
    js["category"]       = descriptor->category.empty()
                               ? std::string("Uncategorized")
                               : descriptor->category;
    nlohmann::json fields = nlohmann::json::array();
    if (descriptor->schema) {
        for (const auto& field : descriptor->schema->fields) {
            fields.push_back(field_to_json(field));
        }
    }
    js["fields"]         = std::move(fields);
    std::cout << js.dump(2) << "\n";
    return 0;
}

} // namespace cli
} // namespace chronon3d
