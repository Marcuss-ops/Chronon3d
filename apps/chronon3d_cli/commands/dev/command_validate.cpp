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
// Props input wired via canonical `load_props_input()`
// (apps/chronon3d_cli/utils/common/props_inline.hpp) which enforces
// mutual-exclusion between `--props-file` and `--props-json` + dispatches
// to `load_props_file` (path) or `load_props_inline` (string) per flag.
// Shared by `chronon render` and `chronon resolve` (AGENTS.md Cat-3
// anti-dup: NO per-command helper duplication).
//
// Exit codes:
//   0 = PASS (decode + validate clean).
//   1 = FAIL (composition unknown | PropsError | CLI input parse error).
//
// Emits JSON error envelope on FAIL with `error`/`composition_id`/`status`
// keys; future-proofed for chaining with shell pipelines.

#include "../../commands.hpp"
#include "../../utils/common/props_file.hpp"
#include "../../utils/common/props_inline.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <iostream>
#include <string>

namespace chronon3d {
namespace cli {

namespace {

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
    const auto loaded = load_props_input(args.props_file, args.props_json);
    if (!loaded.ok) {
        spdlog::error("validate: {}", loaded.error);
        return emit_validate_fail("invalid_props_input", args.comp_id,
                                  loaded.error);
    }
    input.values       = std::move(loaded.props.values);
    input.project_root = std::move(loaded.props.project_root);

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
