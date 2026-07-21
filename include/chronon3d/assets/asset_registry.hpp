#pragma once

#include <chronon3d/assets/asset_metadata.hpp>
#include <chronon3d/core/types/types.hpp>
#include <filesystem>
#include <mutex>
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

// ---------------------------------------------------------------------------
// AssetRegistry -- central store for asset paths and metadata.
//
// PR 4 (de-singletonization):
//   Removed `static AssetRegistry& instance()`.  The host (CLI, test runner)
//   creates an AssetRegistry and passes it explicitly.
//
//   Path resolution is delegated to ::chronon3d::assets::AssetResolver
//   (owned by RenderRuntime as a sibling of AssetRegistry).  Use
//   `runtime.resolver().resolve_lexical(path)` for deterministic,
//   per-engine path resolution.
//
//   Thread-local storage has been removed — all state is passed explicitly.
// ---------------------------------------------------------------------------
class AssetRegistry {
public:
    // ── Construction ───────────────────────────────────────────────────

    AssetRegistry() = default;
    ~AssetRegistry() = default;
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;
    AssetRegistry(AssetRegistry&& other) noexcept
        : m_assets(std::move(other.m_assets))
        , m_by_id(std::move(other.m_by_id)) {}
    AssetRegistry& operator=(AssetRegistry&& other) noexcept {
        if (this != &other) {
            m_assets = std::move(other.m_assets);
            m_by_id = std::move(other.m_by_id);
        }
        return *this;
    }

    // ── Clear (non-static) ────────────────────────────────────────────

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_assets.clear();
        m_by_id.clear();
    }

    // ── Import (non-static) ────────────────────────────────────────────

    /// Auto-detect type from extension and import (non-static, preferred).
    /// Returns std::nullopt for unrecognized extensions.
    std::optional<AssetId> import_by_extension(const std::filesystem::path& resolved) {
        std::string ext = resolved.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(c));
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
            return import_image(resolved);
        if (ext == ".ttf" || ext == ".otf")
            return import_font(resolved);
        if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".mkv")
            return import_video(resolved);
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")
            return import_audio(resolved);
        return std::nullopt;
    }

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

    // ── Metadata accessors (non-static) ────────────────────────────────

    [[nodiscard]] std::optional<AssetMetadata> try_metadata(AssetId id) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_by_id.find(id);
        if (it == m_by_id.end())
            return std::nullopt;
        return m_assets[it->second];
    }

    [[nodiscard]] AssetMetadata metadata_or_throw(AssetId id) const {
        auto meta = try_metadata(id);
        if (!meta)
            throw std::out_of_range("AssetRegistry: unknown AssetId");
        return *meta;
    }

    [[nodiscard]] AssetMetadata metadata(AssetId id) const {
        return metadata_or_throw(id);
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

    [[nodiscard]] std::string get_path(AssetId id) const {
        return metadata(id).path.string();
    }

private:

    AssetId register_asset_unlocked(const std::filesystem::path& path,
                                   AssetType  type,
                                   ColorSpace cs,
                                   AlphaMode  am) {
        const AssetId id = asset_id_from_path(path);
        if (m_by_id.contains(id)) return id;  // deduplicate

        AssetMetadata meta;
        meta.type = type;
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
    std::vector<AssetMetadata>          m_assets;
    std::unordered_map<AssetId, usize>  m_by_id;
};

} // namespace chronon3d
