#pragma once

// =============================================================================
// gpu_asset_cache.hpp — ContentCache: decoded assets → GPU device-local surfaces
//
// Cache family: ContentCache (see cache/cache_taxonomy.hpp).
//
/// Engine-local cache for decoded assets promoted to device-local surfaces.
/// Keys by ContentDigest + PixelFormat + dimensions.  Same key ⇒ same GPU
/// surface, always.  The cache owns logical handles and releases backend
/// storage on eviction; it does not duplicate the RenderSurfaceRegistry or
/// asset resolver.
// =============================================================================

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

namespace chronon3d::runtime {

struct GpuAssetKey {
    assets::ContentDigest content_digest{};
    PixelFormat format{PixelFormat::Unknown};
    std::uint32_t width{0};
    std::uint32_t height{0};

    friend bool operator==(const GpuAssetKey&, const GpuAssetKey&) = default;
};

struct GpuAssetKeyHash {
    std::size_t operator()(const GpuAssetKey& key) const noexcept {
        std::size_t result = 0;
        for (const auto byte : key.content_digest.bytes) {
            result ^= static_cast<std::size_t>(std::to_integer<unsigned char>(byte)) +
                static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        }
        result ^= static_cast<std::size_t>(key.format) +
            static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= key.width + static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        result ^= key.height + static_cast<std::size_t>(0x9e3779b9u) + (result << 6u) + (result >> 2u);
        return result;
    }
};

struct GpuAssetAcquireResult {
    RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
    bool cache_hit{false};
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return handle != kInvalidRenderSurfaceHandle && error.empty();
    }
};

struct GpuAssetCacheStats {
    std::size_t hits{0};
    std::size_t misses{0};
    std::size_t evictions{0};
    std::size_t upload_bytes{0};
    std::size_t resident_bytes{0};
};

/// Engine-local cache for decoded assets promoted to device-local surfaces.
/// The cache owns logical handles and releases backend storage on eviction;
/// it does not duplicate the RenderSurfaceRegistry or asset resolver.
class GpuAssetCache {
public:
    explicit GpuAssetCache(std::size_t budget_bytes = 0) : m_budget_bytes(budget_bytes) {}
    ~GpuAssetCache();

    GpuAssetCache(const GpuAssetCache&) = delete;
    GpuAssetCache& operator=(const GpuAssetCache&) = delete;

    void attach(RenderSurfaceRegistry& registry, graph::RenderBackend& backend) noexcept;
    GpuAssetAcquireResult acquire(
        const GpuAssetKey& key,
        SurfaceDesc desc,
        std::span<const float> rgba);
    void clear() noexcept;

    void set_budget_bytes(std::size_t budget_bytes) noexcept;
    [[nodiscard]] std::size_t budget_bytes() const noexcept { return m_budget_bytes; }
    [[nodiscard]] graph::RenderBackend* backend() const noexcept { return m_backend; }
    [[nodiscard]] GpuAssetCacheStats stats() const noexcept;

private:
    struct Entry {
        RenderSurfaceHandle handle{kInvalidRenderSurfaceHandle};
        std::size_t bytes{0};
        std::list<GpuAssetKey>::iterator lru_position;
    };

    void release_handle(RenderSurfaceHandle handle) noexcept;
    void evict_to_budget_locked();

    mutable std::mutex m_mutex;
    RenderSurfaceRegistry* m_registry{nullptr};
    graph::RenderBackend* m_backend{nullptr};
    std::size_t m_budget_bytes{0};
    std::size_t m_resident_bytes{0};
    std::unordered_map<GpuAssetKey, Entry, GpuAssetKeyHash> m_entries;
    std::list<GpuAssetKey> m_lru;
    GpuAssetCacheStats m_stats{};
};

} // namespace chronon3d::runtime
