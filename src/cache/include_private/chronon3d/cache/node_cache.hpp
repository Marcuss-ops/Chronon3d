#pragma once

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <atomic>
#include <memory>
#include <string>

namespace chronon3d::cache {

struct NodeCacheKey {
    std::string scope;
    Frame       frame{0};
    i32         width{0};
    i32         height{0};
    u64         params_hash{0};
    u64         source_hash{0};
    u64         input_hash{0};

    /// Sub-frame temporal key for motion blur / temporal supersampling.
    /// Static nodes share the same key (frame=0, tick=0) to avoid
    /// re-rendering across motion-blur sub-samples.
    TemporalSampleKey temporal_key{0, 0, 0};

    // Tile-based cache differentiation (Branch 4).
    // Defaults (-1, -1, 0, 0) produce the same digest as before,
    // so non-tile cache keys are unaffected.
    i32         tile_x{-1};
    i32         tile_y{-1};
    i32         tile_size{0};
    u64         tile_hash{0};

    [[nodiscard]] u64 digest() const;
    bool operator==(const NodeCacheKey&) const = default;
};

struct NodeCacheKeyHash {
    size_t operator()(const NodeCacheKey& key) const noexcept {
        return static_cast<size_t>(key.digest());
    }
};

using FramebufferCache = LruCache<NodeCacheKey, std::shared_ptr<Framebuffer>, NodeCacheKeyHash>;

class NodeCache {
public:
    using Value = std::shared_ptr<Framebuffer>;

    explicit NodeCache(size_t capacity_bytes = 2048ULL * 1024 * 1024);

    // Moving a NodeCache invalidates the `this` pointer captured by the
    // CacheDiagnostics registration lambdas, causing a use-after-move
    // crash when format_cache_snapshot() is called after a move.
    // The move constructor/assignment are deleted to prevent this.
    NodeCache(NodeCache&&) = delete;
    NodeCache& operator=(NodeCache&&) = delete;

    ~NodeCache();

    [[nodiscard]] Value get(const NodeCacheKey& key);
    void store(const NodeCacheKey& key, Value value);
    
    [[nodiscard]] bool contains(const NodeCacheKey& key) const;
    void clear();
    
    [[nodiscard]] LruCache<NodeCacheKey, Value, NodeCacheKeyHash>::Stats stats() const { return m_cache.stats(); }
    [[nodiscard]] size_t size() const { return m_cache.stats().current_size; }
    
    void set_capacity(size_t capacity_bytes);

    bool erase(const NodeCacheKey& key);

private:
    // m_diag_handle must be declared BEFORE m_cache so it is destroyed
    // AFTER m_cache.  The stats_fn lambda captures `this` and accesses
    // m_cache; destroying m_diag_handle first would unregister the lambda
    // while m_cache is still alive (safe), but the reverse order would
    // leave a dangling lambda in CacheDiagnostics pointing to a dead cache.
    CacheDiagnostics::Handle m_diag_handle;
    FramebufferCache m_cache;

    // Guard against use-after-destroy: the CacheDiagnostics registry may
    // hold a live std::function targeting this NodeCache after the
    // destructor has run.  We use an atomic flag checked by the lambda
    // to return empty stats safely.
    std::atomic<bool> m_diag_alive{true};
};

} // namespace chronon3d::cache
