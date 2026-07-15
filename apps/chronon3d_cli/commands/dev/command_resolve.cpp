// ═══════════════════════════════════════════════════════════════════════════
// command_resolve.cpp — Phase 1d / Increment F
// ───────────────────────────────────────────────────────────────────────────
//
// `chronon resolve <comp_id> [--props-file <path>|--props-json <inline>]`
// — runs the canonical CompositionRegistry::resolve() pipeline and emits
// the full ResolvedCompositionSpec as machine-readable JSON.  Distinct
// from `chronon validate` (which is a PASS/FAIL gate); `chronon resolve`
// is the data dump of the resolved props + metadata + construct status.
//
// Wraps canonical `CompositionRegistry::resolve(name, input)` and
// `CompositionRegistry::descriptor_of(name)` only.  No `CompositionResolveFn`
// lambda or any abandoned abstraction.
//
// Exit codes:
//   0 = PASS (decode + validate clean; resolved JSON emitted).
//   1 = FAIL (composition unknown | PropsError | CLI input parse error).
//
// Props input shared contract (mirrors Increment E validate sub-command):
//   --props-file / --props-json / empty  → same TU-local pipeline.
//   Mutual-exclusion enforced (--props-file and --props-json together
//   is a CLI input error, NOT silent fallback).

#include "../../commands.hpp"
#include "../../utils/common/props_file.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <iostream>
#include <string>

namespace chronon3d {
namespace cli {

namespace {

/// Parse an inline JSON object string into a ValueMap.  Identical to
/// `command_validate.cpp` (TU-local duplicate per AGENTS.md Cat-3 anti-dup).
inline std::pair<bool, std::string>
parse_props_json_string(const std::string& json_text, ValueMap& out) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_text);
    } catch (const std::exception& e) {
        return {false, std::string("invalid JSON: ") + e.what()};
    }
    if (!root.is_object()) {
        return {false, std::string("props JSON must be a flat object")};
    }
    for (auto it = root.begin(); it != root.end(); ++it) {
        const auto& v = it.value();
        if (v.is_string()) {
            out.set(it.key(), v.get<std::string>());
        } else if (v.is_boolean()) {
            out.set(it.key(), v.get<bool>() ? "true" : "false");
        } else if (v.is_number()) {
            out.set(it.key(), v.dump());
        } else {
            return {false, std::string("prop '") + it.key() +
                    "' must be a scalar string / boolean / number"};
        }
    }
    return {true, std::string{}};
}

/// Fill `input` from args (--props-file OR --props-json OR empty).
/// Enforces mutual-exclusion (mirrors Increment E validate).
inline std::pair<bool, std::string>
build_composition_input(const ResolveArgs& args, CompositionInput& input) {
    if (!args.props_file.empty() && !args.props_json.empty()) {
        return {false,
                std::string("--props-file and --props-json are mutually exclusive; pass only one")};
    }
    if (!args.props_file.empty()) {
        const auto loaded = load_props_file(args.props_file);
        if (!loaded.ok) {
            return {false, loaded.error};
        }
        input.values        = std::move(loaded.props.values);
        input.project_root  = std::move(loaded.props.project_root);
    } else if (!args.props_json.empty()) {
        auto result = parse_props_json_string(args.props_json, input.values);
        if (!result.first) {
            return result;
        }
    }
    return {true, std::string{}};
}

/// Serialize CompositionMetadata to JSON (when present in resolved spec).
inline nlohmann::json metadata_to_json(const CompositionMetadata& md) {
    nlohmann::json js;
    js["width"]      = md.width;
    js["height"]     = md.height;
    js["fps"]        = {
        {"numerator",   md.fps.numerator},
        {"denominator", md.fps.denominator}};
    js["duration"]   = md.duration.integral();
    return js;
}

/// Serialize resolved.props.values to JSON, walking descriptor schema.
/// ValueMap doesn't expose iteration publicly (AGENTS.md Cat-3: keep SDK
/// header untouched), so we walk descriptor.schema->fields and call
/// `get_string(field.name)` for each.  For fields without a default and
/// absent in resolved.props.values, emit empty-string marker.
inline nlohmann::json props_to_json(
    const std::optional<CompositionDescriptor>& descriptor,
    const CompositionProps& resolved_props) {
    nlohmann::json js = nlohmann::json::object();
    if (descriptor && descriptor->schema) {
        for (const auto& field : descriptor->schema->fields) {
            if (resolved_props.values.contains(field.name)) {
                js[field.name] = resolved_props.values.get_string(field.name);
            } else if (field.default_value) {
                js[field.name] = *field.default_value;
            } else {
                js[field.name] = std::string{};
            }
        }
    }
    return js;
}

} // namespace

int command_resolve(const CompositionRegistry& registry,
                    const ResolveArgs& args) {
    CompositionInput input;
    const auto input_ok = build_composition_input(args, input);
    if (!input_ok.first) {
        spdlog::error("resolve: {}", input_ok.second);
        nlohmann::json err;
        err["error"]         = "invalid_props_input";
        err["composition_id"] = args.comp_id;
        err["status"]        = "FAIL";
        err["message"]       = input_ok.second;
        std::cout << err.dump(2) << "\n";
        return 1;
    }

    auto result = registry.resolve(args.comp_id, input);
    if (!result) {
        const auto err = result.error();
        spdlog::error("resolve: '{}' props failed [{}]: {}",
                      args.comp_id, err.key, err.message);
        nlohmann::json js;
        js["resolved"]       = false;
        js["composition_id"] = args.comp_id;
        js["error"]          = "PropsError";
        js["key"]            = err.key;
        switch (err.reason) {
            case PropsErrorReason::MissingRequired: js["reason"] = "MissingRequired"; break;
            case PropsErrorReason::BadType:         js["reason"] = "BadType";         break;
            case PropsErrorReason::OutOfRange:      js["reason"] = "OutOfRange";      break;
            case PropsErrorReason::InvalidFormat:   js["reason"] = "InvalidFormat";   break;
        }
        js["message"]        = err.message;
        js["status"]         = "FAIL";
        std::cout << js.dump(2) << "\n";
        return 1;
    }

    // Success path: emit {resolved, composition_id, props, metadata, status}.
    // Use descriptor_of(comp_id) for schema-walking (graceful fallback if
    // schema is std::nullopt → emit empty props object).
    auto descriptor = registry.descriptor_of(args.comp_id);
    nlohmann::json js;
    js["resolved"]        = static_cast<bool>(result->construct);
    js["composition_id"]  = args.comp_id;
    js["props"]           = props_to_json(descriptor, result->props);
    if (result->metadata) {
        js["metadata"]    = metadata_to_json(*result->metadata);
    }
    js["status"]          = "PASS";
    std::cout << js.dump(2) << "\n";
    return 0;
}

} // namespace cli
} // namespace chronon3d
