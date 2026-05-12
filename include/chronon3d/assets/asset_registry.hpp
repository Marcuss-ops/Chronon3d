#pragma once

#include <chronon3d/assets/asset_metadata.hpp>
#include <chronon3d/core/types.hpp>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace chronon3d {

using AssetId = u64;

// Stable hash of a path string (not file contents).
// Same canonical path → same id across runs.
inline AssetId asset_id_from_path(const std::filesystem::path& path) {
    const std::string s = path.string();
    return XXH3_64bits(s.data(), s.size());
}

// Legacy helper kept for backward compat.
inline AssetId hash_asset_id(const std::string& name) {
    return XXH3_64bits(name.data(), name.size());
}

// ---------------------------------------------------------------------------
// AssetRegistry -- central store for asset paths and metadata.
// No file I/O here: the renderer/loader resolves assets on first use.
// Calling import_image twice with the same path returns the same AssetId.
// ---------------------------------------------------------------------------
class AssetRegistry {
public:
    // --- New API -----------------------------------------------------------

    AssetId import_image(const std::filesystem::path& path) {
        return register_asset(path, AssetType::Image, ColorSpace::SRGB, AlphaMode::Straight);
    }

    AssetId import_font(const std::filesystem::path& path) {
        return register_asset(path, AssetType::Font, ColorSpace::LinearSRGB, AlphaMode::None);
    }

    AssetId import_video(const std::filesystem::path& path) {
        return register_asset(path, AssetType::Video, ColorSpace::SRGB, AlphaMode::Straight);
    }

    AssetId import_audio(const std::filesystem::path& path) {
        return register_asset(path, AssetType::Audio, ColorSpace::LinearSRGB, AlphaMode::None);
    }

    [[nodiscard]] const AssetMetadata& metadata(AssetId id) const {
        const auto it = m_by_id.find(id);
        if (it == m_by_id.end())
            throw std::out_of_range("AssetRegistry: unknown AssetId");
        return m_assets[it->second];
    }

    [[nodiscard]] std::optional<AssetId> find_by_path(const std::filesystem::path& path) const {
        const AssetId id = asset_id_from_path(path);
        if (m_by_id.contains(id)) return id;
        return std::nullopt;
    }

    [[nodiscard]] bool contains(AssetId id) const { return m_by_id.contains(id); }
    [[nodiscard]] usize size() const { return m_assets.size(); }

    // --- Legacy API (backward compat) ------------------------------------

    void declare_image(const std::string& /*name*/, const std::string& path) {
        import_image(path);
    }

    void declare_mesh(const std::string& /*name*/, const std::string& path) {
        register_asset(path, AssetType::Mesh, ColorSpace::LinearSRGB, AlphaMode::None);
    }

    void resolve_all() {}  // no-op: resolution is lazy in the renderer

    [[nodiscard]] bool is_loaded(AssetId id) const { return contains(id); }
    [[nodiscard]] bool is_loaded(const std::string& name) const {
        return contains(hash_asset_id(name));
    }

    [[nodiscard]] std::string get_path(AssetId id) const {
        return metadata(id).path.string();
    }

private:
    AssetId register_asset(const std::filesystem::path& path,
                           AssetType  type,
                           ColorSpace cs,
                           AlphaMode  am) {
        const AssetId id = asset_id_from_path(path);
        if (m_by_id.contains(id)) return id;  // deduplicate

        AssetMetadata meta;
        meta.type        = type;
        meta.path        = path;
        meta.color_space = cs;
        meta.alpha_mode  = am;

        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx",
                      static_cast<unsigned long long>(id));
        meta.path_hash = buf;

        m_by_id[id] = m_assets.size();
        m_assets.push_back(std::move(meta));
        return id;
    }

    std::vector<AssetMetadata>          m_assets;
    std::unordered_map<AssetId, usize>  m_by_id;
};

} // namespace chronon3d
