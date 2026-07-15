// ═══════════════════════════════════════════════════════════════════════════
// props_inline.hpp — Phase 1c / Increment B
// ───────────────────────────────────────────────────────────────────────────
//
// CLI helpers for loading composition props from EITHER a path
// (`--props-file <path>`) OR an inline JSON object string
// (`--props-json <inline>`).  Companion to `props_file.hpp`
// (path-sourced loader), shares the canonical `detail::parse_props_flat_object()`
// helper with it to avoid 3-callsite duplication (AGENTS.md Cat-3 anti-dup).
//
// Three public entry points, all canonical and shared between the
// `chronon render`, `chronon validate` and `chronon resolve` commands:
//
//   1. `load_props_inline(text)`             — inline JSON string → props.
//   2. `load_props_input(file_path, json)`   — mutual-exclusion dispatch.
//
// `load_props_file(path)` remains the canonical entry point for the
// file-sourced variant (defined in `props_file.hpp`).

#pragma once

#include <chronon3d/timeline/composition_props.hpp>

#include "props_file.hpp"  // for PropsFileResult + detail::parse_props_flat_object

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace chronon3d::cli {

/// Load a flat JSON object from an inline string into the canonical
/// CompositionProps/ValueMap bridge.  Mirrors `load_props_file` semantics:
/// string | bool | number scalar values only; nested objects + arrays +
/// null are rejected (canonical PropsCodec remains the decoding authority).
///
/// `project_root` is left empty (inline JSON has no associated on-disk
/// directory); consumers of the returned `props` default to the current
/// working directory.
inline PropsFileResult load_props_inline(const std::string& json_text) {
    PropsFileResult result;
    if (json_text.empty()) {
        // Empty input is silent-OK (canonical defaults path); matches
        // `load_props_file` precedent at apps/chronon3d_cli/utils/common/props_file.hpp.
        result.ok = true;
        return result;
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_text);
    } catch (const std::exception& e) {
        result.error = "Invalid inline JSON: " + std::string(e.what());
        return result;
    }

    const auto [ok, err] = detail::parse_props_flat_object(
        root, result.props, result.keys);
    if (!ok) {
        result.error = "Inline JSON: " + err;
        return result;
    }
    result.ok = true;
    return result;
}

/// Mutual-exclusion dispatcher: returns the merged props from EITHER
/// `--props-file <path>` OR `--props-json <inline>` (NEVER both, NEVER
/// neither when caller wants empty defaults).
///
///   caller's args         behavior
/// ────────────────────    ──────────────────────────────────────────────
///   both empty            silent-OK, returns empty props (defaults path)
///   only file set         delegates to `load_props_file`
///   only json set         delegates to `load_props_inline`
///   both set              explicit ERROR (mutual exclusion — CLI input bug)
///
/// Used by 3 commands (`chronon render`, `chronon validate`,
/// `chronon resolve`) to avoid duplicating the dispatch logic — see
/// AGENTS.md Cat-3 anti-dup (`Non duplicare resolver/sampler/checklist`).
inline PropsFileResult load_props_input(const std::string& file_path,
                                        const std::string& json_text) {
    if (!file_path.empty() && !json_text.empty()) {
        PropsFileResult result;
        result.error = "--props-file and --props-json are mutually exclusive; pass only one";
        return result;
    }
    if (!file_path.empty()) {
        return load_props_file(file_path);
    }
    // path empty: delegate to inline (json_text empty = defaults path → ok=true)
    return load_props_inline(json_text);
}

} // namespace chronon3d::cli
