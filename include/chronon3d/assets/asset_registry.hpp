#pragma once

#include <chronon3d/core/types.hpp>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <stdexcept>

namespace chronon3d {

using AssetId = std::string;

enum class AssetType {
    Image,
    Mesh,
    Font
};

struct AssetRequest {
    AssetId id;
    std::string path;
    AssetType type;
};

class AssetRegistry {
public:
    void declare_image(const AssetId& id, const std::string& path) {
        m_pending.push_back({id, path, AssetType::Image});
    }

    void declare_mesh(const AssetId& id, const std::string& path) {
        m_pending.push_back({id, path, AssetType::Mesh});
    }

    void resolve_all() {
        for (const auto& req : m_pending) {
            // In a real implementation, we would call stb_image or assimp here.
            // For now, we just track that they were "loaded".
            m_loaded_assets[req.id] = req.path;
        }
        m_pending.clear();
    }

    [[nodiscard]] bool is_loaded(const AssetId& id) const {
        return m_loaded_assets.find(id) != m_loaded_assets.end();
    }

    [[nodiscard]] std::string get_path(const AssetId& id) const {
        auto it = m_loaded_assets.find(id);
        if (it == m_loaded_assets.end()) {
            throw std::runtime_error("Asset not loaded: " + id);
        }
        return it->second;
    }

private:
    std::vector<AssetRequest> m_pending;
    std::map<AssetId, std::string> m_loaded_assets;
};

} // namespace chronon3d
