#pragma once

#include <chronon3d/timeline/composition_props.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct PropsFileResult {
    bool ok{false};
    CompositionProps props{};
    std::vector<std::string> keys;
    std::string error;
};

namespace detail {

/// Internal helper shared by `load_props_file` (path-sourced) and
/// `load_props_inline` (string-sourced). Walks a flat nlohmann::json
/// object and populates `props.values` (string | bool | number only) +
/// `keys` (insertion order).  Nested objects, arrays and null are
/// rejected deliberately: typed PropsCodec implementations remain the
/// single authority for decoding and validation.
///
/// **Internal aid only**: prefer the public load_props_file /
/// load_props_inline / load_props_input entry points; this helper is
/// exposed solely to avoid 3-callsite duplication of the same iteration
/// loop (AGENTS.md Cat-3 anti-dup).
inline std::pair<bool, std::string>
parse_props_flat_object(const nlohmann::json& root,
                        CompositionProps& props,
                        std::vector<std::string>& keys) {
    if (!root.is_object()) {
        return {false, std::string("props JSON root must be a flat object")};
    }
    keys.reserve(root.size());
    for (auto it = root.begin(); it != root.end(); ++it) {
        const auto& value = it.value();
        if (value.is_string()) {
            props.values.set(it.key(), value.get<std::string>());
        } else if (value.is_boolean()) {
            props.values.set(it.key(), value.get<bool>() ? "true" : "false");
        } else if (value.is_number()) {
            props.values.set(it.key(), value.dump());
        } else {
            return {false, std::string("prop '") + it.key() +
                    "' must be a scalar string, boolean or number"};
        }
        keys.push_back(it.key());
    }
    return {true, std::string{}};
}

} // namespace detail

/// Load a flat JSON object from a path into the canonical
/// CompositionProps/ValueMap bridge.  Refactored to delegate the
/// iteration loop to `detail::parse_props_flat_object()` (Phase 1c
/// Increment B — 3-callsite Cat-3 anti-dup refactor with `load_props_inline`).
inline PropsFileResult load_props_file(const std::filesystem::path& path) {
    PropsFileResult result;
    if (path.empty()) {
        result.ok = true;
        return result;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        result.error = "Could not open props file: " + path.string();
        return result;
    }

    nlohmann::json root;
    try {
        input >> root;
    } catch (const std::exception& e) {
        result.error = "Invalid JSON in props file '" + path.string() + "': " + e.what();
        return result;
    }

    const auto [ok, err] = detail::parse_props_flat_object(root, result.props, result.keys);
    if (!ok) {
        result.error = "Props file '" + path.string() + "': " + err;
        return result;
    }

    result.props.project_root = path.parent_path();
    result.ok = true;
    return result;
}

} // namespace chronon3d::cli
