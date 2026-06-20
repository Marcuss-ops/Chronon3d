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

// ---------------------------------------------------------------------------
// AssetRegistry -- central store for asset paths and metadata.
//
// PR 4 (de-singletonization):
//   Removed `static AssetRegistry& instance()`.  The host (CLI, test runner)
//   creates an AssetRegistry and sets the default assets root via
//   `chronon3d::detail::set_default_assets_root(root)` before any rendering.
//
//   Deep rendering code calls the two-argument `resolve_asset_path(root, path)`
//   (preferred) or the single-argument overload backed by the default root.
//
//   Thread-local storage has been removed — all state is either passed
//   explicitly or stored in the process-wide default root.
// ---------------------------------------------------------------------------
class AssetRegistry {
public:
    // ── Construction ───────────────────────────────────────────────────

    AssetRegistry() = default;
    ~AssetRegistry() = default;
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    // ── Mount / clear (non-static) ─────────────────────────────────────

    /// Mount the default assets root.  Used as a fallback when no
    /// explicit assets_root is available via resolve_asset_path().
    void mount(const std::filesystem::path& root_path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_root_path = root_path;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_assets.clear();
        m_by_id.clear();
        m_root_path.clear();
    }

    // ── Non-static resolve (preferred — use from RenderContext) ────────

    [[nodiscard]] std::string resolve_path(const std::filesystem::path& relative_path) const {
        if (relative_path.is_absolute()) {
            return relative_path.lexically_normal().string();
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_root_path.empty()) {
            return (m_root_path / relative_path).lexically_normal().string();
        }
        return relative_path.lexically_normal().string();
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
    std::filesystem::path               m_root_path;
    std::vector<AssetMetadata>          m_assets;
    std::unordered_map<AssetId, usize>  m_by_id;
};

// ── Per-render-job scoped asset resolution (non-TLS) ──────────────────
//
// Prefer the two-argument resolve_asset_path(root, path) to resolve against
// an explicit assets root directory.  The single-argument overload uses the
// process-wide default root set via detail::set_default_assets_root().

/// Resolve a relative path against an assets root directory.
/// Returns the relative_path unchanged if empty, absolute, or if assets_root is empty.
[[nodiscard]] inline std::string resolve_asset_path(
    const std::string& assets_root,
    const std::string& relative_path)
{
    if (relative_path.empty() || std::filesystem::path(relative_path).is_absolute()) {
        return relative_path;
    }
    if (assets_root.empty()) {
        return relative_path;
    }
    return (std::filesystem::path(assets_root) / relative_path)
        .lexically_normal().string();
}

// ── Default assets root for deep rendering code ────────────────────
//
// Set once at CLI init time (render_job_setup.cpp).  Deep rendering code
// (font_engine, text_rasterizer, etc.) calls resolve_asset_path() which
// reads this as a fallback when no explicit assets_root is available.
//
// Thread safety: assumes a single writer during startup before any
// concurrent reads.  Do NOT call set_default_assets_root() from
// multiple threads.

namespace chronon3d::detail {
    inline std::string g_default_assets_root;
    inline void set_default_assets_root(std::string root) {
        g_default_assets_root = std::move(root);
    }
}

/// Resolve a relative path using the default assets root (set at startup).
/// Falls back to returning the path unchanged if no default root is set.
[[nodiscard]] inline std::string resolve_asset_path(const std::string& relative_path) {
    return resolve_asset_path(detail::g_default_assets_root, relative_path);
}

} // namespace chronon3d
