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
// Props input wired via canonical `load_props_input()`
// (apps/chronon3d_cli/utils/common/props_inline.hpp) — same mutual-exclusion
// dispatch as `chronon validate` and `chronon render`.  AGENTS.md Cat-3
// anti-dup: NO per-command helper duplication (refactored in Phase 1c
// Increment B alongside render's `--props-json` flag).

#include "../../commands.hpp"
#include "../../utils/common/props_file.hpp"
#include "../../utils/common/props_inline.hpp"

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
    const auto loaded = load_props_input(args.props_file, args.props_json);
    if (!loaded.ok) {
        spdlog::error("resolve: {}", loaded.error);
        nlohmann::json err;
        err["error"]         = "invalid_props_input";
        err["composition_id"] = args.comp_id;
        err["status"]        = "FAIL";
        err["message"]       = loaded.error;
        std::cout << err.dump(2) << "\n";
        return 1;
    }
    input.values       = std::move(loaded.props.values);
    input.project_root = std::move(loaded.props.project_root);

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
