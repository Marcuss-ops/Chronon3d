#pragma once

#include <chronon3d/assets/asset_metadata.hpp>
#include <chronon3d/core/types/types.hpp>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

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
    static AssetRegistry& instance() {
        static AssetRegistry inst;
        return inst;
    }

    static void mount(const std::filesystem::path& root_path) {
        auto& inst = instance();
        std::lock_guard<std::mutex> lock(inst.m_mutex);
        inst.m_root_path = root_path;
    }

    static void clear() {
        auto& inst = instance();
        std::lock_guard<std::mutex> lock(inst.m_mutex);
        inst.m_assets.clear();
        inst.m_by_id.clear();
        inst.m_root_path.clear();
    }

    static std::string resolve(const std::filesystem::path& relative_path) {
        auto& inst = instance();
        std::lock_guard<std::mutex> lock(inst.m_mutex);
        if (relative_path.is_absolute()) {
            return relative_path.lexically_normal().string();
        }
        if (inst.m_root_path.empty()) {
            return relative_path.lexically_normal().string();
        }
        return (inst.m_root_path / relative_path).lexically_normal().string();
    }

    // --- New API -----------------------------------------------------------

    AssetId import_image(const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return register_asset_unlocked(path, AssetType::Image, ColorSpace::SRGB, AlphaMode::Straight);
    }

    AssetId import_font(const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return register_asset_unlocked(path, AssetType::Font, ColorSpace::LinearSRGB, AlphaMode::None);
    }

    AssetId import_video(const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return register_asset_unlocked(path, AssetType::Video, ColorSpace::SRGB, AlphaMode::Straight);
    }

    AssetId import_audio(const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return register_asset_unlocked(path, AssetType::Audio, ColorSpace::LinearSRGB, AlphaMode::None);
    }

    [[nodiscard]] AssetMetadata metadata(AssetId id) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_by_id.find(id);
        if (it == m_by_id.end())
            throw std::out_of_range("AssetRegistry: unknown AssetId");
        return m_assets[it->second];
    }

    [[nodiscard]] std::optional<AssetId> find_by_path(const std::filesystem::path& path) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const AssetId id = asset_id_from_path(path);
        if (m_by_id.contains(id)) return id;
        return std::nullopt;
    }

    [[nodiscard]] bool contains(AssetId id) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_by_id.contains(id);
    }

    [[nodiscard]] usize size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_assets.size();
    }

    [[nodiscard]] std::vector<AssetMetadata> assets() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_assets;
    }

    // --- Legacy API (backward compat) ------------------------------------

    void declare_image(const std::string& /*name*/, const std::string& path) {
        import_image(path);
    }

    void declare_mesh(const std::string& /*name*/, const std::string& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        register_asset_unlocked(path, AssetType::Mesh, ColorSpace::LinearSRGB, AlphaMode::None);
    }

    void resolve_all() {}  // no-op: resolution is lazy in the renderer

    [[nodiscard]] bool is_loaded(AssetId id) const { return contains(id); }
    [[nodiscard]] bool is_loaded(const std::string& name) const {
        return contains(hash_asset_id(name));
    }

    [[nodiscard]] std::string get_path(AssetId id) const {
        return metadata(id).path.string();
    }

    AssetRegistry() = default;
    ~AssetRegistry() = default;
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

private:

    AssetId register_asset_unlocked(const std::filesystem::path& path,
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

    mutable std::mutex                  m_mutex;
    std::filesystem::path               m_root_path;
    std::vector<AssetMetadata>          m_assets;
    std::unordered_map<AssetId, usize>  m_by_id;
};

// Free function asset() helper
inline std::string asset(const std::string& path) {
    std::string resolved = AssetRegistry::resolve(path);
    std::filesystem::path p(resolved);
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(c));

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
        AssetRegistry::instance().import_image(resolved);
    } else if (ext == ".ttf" || ext == ".otf") {
        AssetRegistry::instance().import_font(resolved);
    } else if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".mkv") {
        AssetRegistry::instance().import_video(resolved);
    } else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
        AssetRegistry::instance().import_audio(resolved);
    }
    return resolved;
}

} // namespace chronon3d
