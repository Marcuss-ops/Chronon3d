#pragma once

// =============================================================================
// frame_cache.hpp — LruCache-backed cache of rendered frames per (composition,
// scene_hash, render_hash, frame, temporal_key, dimensions).
//
// Commit 2 of the cache refactor: previously this was an `unordered_map` with
// NO eviction (unbounded growth).  After the collapse it is backed by LruCache
// in CapacityMode::Weight (byte-weighted via Framebuffer::size_bytes()) with
// capacity resolved centrally by resolve_cache_policy(CacheDomain::RenderedFrames).
//
// Breaking API change (zero prod callers currently):
//   find() now returns std::shared_ptr<Framebuffer> (nullptr on miss) instead
//   of a pointer into a stable unordered_map.  Shared_ptr is the idiomatic
//   alternative since LruCache returns values by value (no pointer stability).
// =============================================================================

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <memory>
#include <string>

namespace chronon3d::cache {

struct FrameCacheKey {
    std::string composition_id;
    Frame       frame{0};
    i32         width{0};
    i32         height{0};
    u64         scene_hash{0};
    u64         render_hash{0};

    /// Sub-frame temporal key for motion blur / temporal supersampling.
    /// Static frames share the same key (frame=0, tick=0, version=0).
    TemporalSampleKey temporal_key{0, 0, 0};

    [[nodiscard]] u64 digest() const;
    [[nodiscard]] bool operator==(const FrameCacheKey&) const = default;
};

struct FrameCacheKeyHash {
    [[nodiscard]] size_t operator()(const FrameCacheKey& key) const noexcept;
};

/// Thread-safe (sharded) LRU-bounded cache of rendered frames.  See file header.
class FrameCache {
public:
    using Value = std::shared_ptr<Framebuffer>;

    /// Construct a cache with up to `max_entries` entries split across
    /// `num_shards` shards.  When `max_entries == 0` the cap is resolved
    /// centrally via resolve_cache_policy(CacheDomain::RenderedFrames).
    explicit FrameCache(size_t max_entries = 0, size_t num_shards = 2);
    FrameCache(FrameCache&&) noexcept = default;
    FrameCache& operator=(FrameCache&&) noexcept = default;

    [[nodiscard]] bool contains(const FrameCacheKey& key) const;

    /// Look up a key.  Returns the cached `shared_ptr<Framebuffer>` or
    /// nullptr on miss.  Promotes the entry to MRU on hit, so this method
    /// is intentionally non-const.
    [[nodiscard]] std::shared_ptr<Framebuffer> find(const FrameCacheKey& key);

    /// Insert/replace an entry.  If the cap is exceeded, the LRU entry is
    /// evicted (see LruCache for full semantics).
    void store(FrameCacheKey key, Value value);

    [[nodiscard]] bool erase(const FrameCacheKey& key);
    void clear();

    [[nodiscard]] size_t size() const;
    [[nodiscard]] LruCache<FrameCacheKey, Value, FrameCacheKeyHash>::Stats stats() const;

private:
    LruCache<FrameCacheKey, Value, FrameCacheKeyHash> m_cache;
    CacheDiagnostics::Handle m_diag_handle;
};

} // namespace chronon3d::cache
