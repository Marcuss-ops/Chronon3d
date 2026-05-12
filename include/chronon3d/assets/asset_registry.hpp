#pragma once

#include <chronon3d/core/types.hpp>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <unordered_map>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace chronon3d {

using AssetId = u64;

inline AssetId hash_asset_id(const std::string& name) {
    return XXH3_64bits(name.data(), name.size());
}

enum class AssetType {
    Image,
    Mesh,
    Font
};

struct AssetRequest {
    AssetId id;
    std::string name;
    std::string path;
    AssetType type;
};

class AssetRegistry {
public:
    void declare_image(const std::string& name, const std::string& path) {
        m_pending.push_back({hash_asset_id(name), name, path, AssetType::Image});
    }

    void declare_mesh(const std::string& name, const std::string& path) {
        m_pending.push_back({hash_asset_id(name), name, path, AssetType::Mesh});
    }

    void resolve_all() {
        for (const auto& req : m_pending) {
            // Resolve path relative to asset root if needed
            m_loaded_paths[req.id] = req.path;
            
            if (req.type == AssetType::Image) {
                // Placeholder for stb_image integration
                // auto pixels = stbi_load(req.path.c_str(), &w, &h, &comp, 4);
            }
        }
        m_pending.clear();
    }

    [[nodiscard]] bool is_loaded(AssetId id) const {
        return m_loaded_paths.contains(id);
    }

    [[nodiscard]] bool is_loaded(const std::string& name) const {
        return is_loaded(hash_asset_id(name));
    }

    [[nodiscard]] std::string get_path(AssetId id) const {
        auto it = m_loaded_paths.find(id);
        if (it == m_loaded_paths.end()) {
            throw std::runtime_error("Asset not loaded");
        }
        return it->second;
    }

private:
    std::vector<AssetRequest> m_pending;
    std::unordered_map<AssetId, std::string> m_loaded_paths;
};

} // namespace chronon3d
