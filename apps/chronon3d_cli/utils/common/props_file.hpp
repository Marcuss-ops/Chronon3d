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

/// Load a flat JSON object into the canonical CompositionProps/ValueMap bridge.
/// Values may be strings, booleans or numbers. Nested objects, arrays and null
/// are rejected deliberately: typed PropsCodec implementations remain the
/// single authority for decoding and validation.
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

    if (!root.is_object()) {
        result.error = "Props file root must be a JSON object: " + path.string();
        return result;
    }

    result.keys.reserve(root.size());
    for (auto it = root.begin(); it != root.end(); ++it) {
        const auto& value = it.value();
        if (value.is_string()) {
            result.props.values.set(it.key(), value.get<std::string>());
        } else if (value.is_boolean()) {
            result.props.values.set(it.key(), value.get<bool>() ? "true" : "false");
        } else if (value.is_number()) {
            result.props.values.set(it.key(), value.dump());
        } else {
            result.error = "Prop '" + it.key() +
                           "' must be a scalar string, boolean or number";
            return result;
        }
        result.keys.push_back(it.key());
    }

    result.props.project_root = path.parent_path();
    result.ok = true;
    return result;
}

} // namespace chronon3d::cli
