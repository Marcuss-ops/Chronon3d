// ═══════════════════════════════════════════════════════════════════════════
// command_validate.cpp — Phase 1d / Increment E
// ───────────────────────────────────────────────────────────────────────────
//
// `chronon validate <comp_id> [--props-file <path>|--props-json <inline>]`
// — runs the canonical CompositionRegistry::resolve() pipeline and emits
// a `{valid: bool, ...}` JSON gate.  Exits 0 if decode + validate succeeded,
// 1 otherwise.  Validates against the canonical PropsCodec pipeline (NOT a
// local re-implementation of any codec/validator).
//
// Props input shared contract (per Increment E/F cat-3 design):
//   --props-file <path>    → reuses canonical `load_props_file()`
//                            (apps/chronon3d_cli/utils/common/props_file.hpp)
//   --props-json <inline>  → TU-local `parse_props_json_string()`
//                            helper (per Cat-3 anti-dup, duplicated
//                            in command_resolve.cpp at the same TU-local
//                            interface).
//   (no flag)              → empty input = canonical defaults.
//
// Exit codes:
//   0 = PASS (decode + validate clean).
//   1 = FAIL (composition unknown | PropsError | CLI input parse error).
//
// Emits JSON error envelope on FAIL with `error`/`composition_id`/`status`
// keys; future-proofed for chaining with shell pipelines.

#include "../../commands.hpp"
#include "../../utils/common/props_file.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <iostream>
#include <string>

namespace chronon3d {
namespace cli {

namespace {

/// Parse an inline JSON object string into a ValueMap.  Matches the
/// scalar semantics of `load_props_file` (string | bool | number); nested
/// objects + arrays + null are rejected (per the PropsCodec authority
/// boundary).  Returns: `ok=true` + populated ValueMap on success;
/// `ok=false` + `error` string otherwise.  TU-local helper duplicated
/// in command_resolve.cpp (per AGENTS.md Cat-3 anti-dup — no new shared
/// utility for 2 callsites).
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
/// Returns `ok=true` on success; `ok=false` + populated `error` otherwise.
/// Enforces mutual-exclusion: only one of `--props-file` or `--props-json`
/// can be set; passing both is a CLI input error (NOT silent fallback).
inline std::pair<bool, std::string>
build_composition_input(const ValidateArgs& args, CompositionInput& input) {
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
    // (else) input remains empty = canonical defaults.
    return {true, std::string{}};
}

/// Emit a structured FAIL JSON envelope and return 1.
inline int emit_validate_fail(const std::string& error_code,
                              const std::string& comp_id,
                              const std::string& details) {
    nlohmann::json err;
    err["error"]         = error_code;
    err["composition_id"] = comp_id;
    err["status"]        = "FAIL";
    err["message"]       = details;
    std::cout << err.dump(2) << "\n";
    return 1;
}

} // namespace

int command_validate(const CompositionRegistry& registry,
                     const ValidateArgs& args) {
    CompositionInput input;
    const auto input_ok = build_composition_input(args, input);
    if (!input_ok.first) {
        spdlog::error("validate: {}", input_ok.second);
        return emit_validate_fail("invalid_props_input", args.comp_id,
                                  input_ok.second);
    }

    auto result = registry.resolve(args.comp_id, input);
    if (!result) {
        const auto err = result.error();
        spdlog::error("validate: '{}' props failed [{}]: {}",
                      args.comp_id, err.key, err.message);
        nlohmann::json js;
        js["valid"]         = false;
        js["composition_id"] = args.comp_id;
        js["error"]         = "PropsError";
        js["key"]           = err.key;
        switch (err.reason) {
            case PropsErrorReason::MissingRequired: js["reason"] = "MissingRequired"; break;
            case PropsErrorReason::BadType:         js["reason"] = "BadType";         break;
            case PropsErrorReason::OutOfRange:      js["reason"] = "OutOfRange";      break;
            case PropsErrorReason::InvalidFormat:   js["reason"] = "InvalidFormat";   break;
        }
        js["message"]       = err.message;
        js["status"]        = "FAIL";
        std::cout << js.dump(2) << "\n";
        return 1;
    }

    nlohmann::json js;
    js["valid"]         = static_cast<bool>(result->construct);
    js["composition_id"] = args.comp_id;
    js["props_keys_count"] = result->props.values.size();
    js["status"]        = "PASS";
    std::cout << js.dump(2) << "\n";
    return 0;
}

} // namespace cli
} // namespace chronon3d
