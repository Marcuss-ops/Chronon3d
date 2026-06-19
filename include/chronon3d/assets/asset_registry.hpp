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
//   creates an AssetRegistry and sets it as the thread-local pointer via
//   `AssetRegistry::set_thread_local(registry)` before any rendering.
//
//   `resolve()` remains a static method and reads from the thread-local
//   pointer — this keeps the deep rendering code (text rasterizers, font
//   engines) unchanged while respecting Rule 3 (no static global singletons).
//
//   Thread-local root (set_thread_local_root / clear_thread_local_root)
//   is unchanged — per-composition asset roots still use this mechanism.
// ---------------------------------------------------------------------------
class AssetRegistry {
public:
    // ── Thread-local pointer (replaces instance()) ──────────────────────
    // Set by the host before any rendering or asset resolution.  resolve()
    // and assets() read from this pointer.  If null, resolve() returns the
    // path as-is and assets() returns an empty vector.

    static AssetRegistry*& tls_registry_ptr() {
        thread_local AssetRegistry* ptr = nullptr;
        return ptr;
    }

    /// Set the registry for the current thread.  Called by the host
    /// at startup and before each render job.
    static void set_thread_local(AssetRegistry& reg) {
        tls_registry_ptr() = &reg;
    }

    /// Clear the thread-local registry pointer.
    static void clear_thread_local() {
        tls_registry_ptr() = nullptr;
    }

    /// Return the thread-local registry, or nullptr if none set.
    static AssetRegistry* get_thread_local() {
        return tls_registry_ptr();
    }

    // ── Construction ───────────────────────────────────────────────────

    AssetRegistry() = default;
    ~AssetRegistry() = default;
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    // ── Mount / clear (non-static) ─────────────────────────────────────

    /// Mount the global (default) assets root.  Used as a fallback when no
    /// per-render-job thread-local root is active.
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

    // ── Per-thread assets root (static, unchanged) ─────────────────────

    /// Set the per-thread assets root for the current render job.
    /// When set, resolve() prefers this root over the registry's global root.
    static void set_thread_local_root(const std::filesystem::path& root) {
        tls_current_root() = root;
    }

    /// Clear the per-thread assets root.
    static void clear_thread_local_root() {
        tls_current_root().clear();
    }

    // ── Resolve (static — reads from thread-local pointer + root) ──────

    static std::string resolve(const std::filesystem::path& relative_path) {
        // Per-render-job thread-local root takes priority.
        // Read TLS before acquiring the mutex — thread-local is inherently
        // thread-safe (only the owning thread can read/write its own copy).
        const auto& tls_root = tls_current_root();
        if (relative_path.is_absolute()) {
            return relative_path.lexically_normal().string();
        }
        if (!tls_root.empty()) {
            return (tls_root / relative_path).lexically_normal().string();
        }
        AssetRegistry* reg = tls_registry_ptr();
        if (reg) {
            std::lock_guard<std::mutex> lock(reg->m_mutex);
            if (!reg->m_root_path.empty()) {
                return (reg->m_root_path / relative_path).lexically_normal().string();
            }
        }
        return relative_path.lexically_normal().string();
    }

    // ── Import (non-static) ────────────────────────────────────────────

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

    // ── assets() — also available as static (reads thread-local) ───────

    [[nodiscard]] std::vector<AssetMetadata> assets() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_assets;
    }

    /// Static accessor for deep code that can't receive a reference.
    /// Reads from the thread-local registry pointer.  Returns empty
    /// vector if no registry is set on the current thread.
    [[nodiscard]] static std::vector<AssetMetadata> current_assets() {
        AssetRegistry* reg = tls_registry_ptr();
        if (!reg) return {};
        std::lock_guard<std::mutex> lock(reg->m_mutex);
        return reg->m_assets;
    }

    [[nodiscard]] std::string get_path(AssetId id) const {
        return metadata(id).path().string();
    }

private:

    AssetId register_asset_unlocked(const std::filesystem::path& path,
                                   AssetType  type,
                                   ColorSpace cs,
                                   AlphaMode  am) {
        const AssetId id = asset_id_from_path(path);
        if (m_by_id.contains(id)) return id;  // deduplicate

        AssetMetadata meta;
        meta.set_type(type;
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

    /// Thread-local per-render-job assets root, set by the render pipeline
    /// before executing the graph and cleared afterward.  resolve() checks
    /// this first, so concurrent render jobs on different threads never
    /// interfere with each other's asset resolution.
    static std::filesystem::path& tls_current_root() {
        thread_local std::filesystem::path s_root;
        return s_root;
    }
};

// ── Per-render-job scoped asset resolution ──────────────────────────
//
// The assets root for the current composition is set via
// AssetRegistry::set_thread_local_root() before graph execution
// and cleared afterward.  This prevents races when concurrent
// render jobs use different assets directories.
//
// Usage:
//   AssetRegistry::set_thread_local_root(comp.assets_root());
//   // ... render ...
//   AssetRegistry::clear_thread_local_root();

// Free function asset() helper — uses thread-local registry
inline std::string asset(const std::string& path) {
    std::string resolved = AssetRegistry::resolve(path);
    std::filesystem::path p(resolved);
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(c));

    AssetRegistry* reg = AssetRegistry::get_thread_local();
    if (reg) {
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
            reg->import_image(resolved);
        } else if (ext == ".ttf" || ext == ".otf") {
            reg->import_font(resolved);
        } else if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".mkv") {
            reg->import_video(resolved);
        } else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
            reg->import_audio(resolved);
        }
    }
    return resolved;
}

} // namespace chronon3d
