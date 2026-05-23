#pragma once

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/core/framebuffer.hpp>
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

    [[nodiscard]] u64 digest() const;
    bool operator==(const NodeCacheKey&) const = default;
};

struct NodeCacheKeyHash {
    size_t operator()(const NodeCacheKey& key) const noexcept {
        return static_cast<size_t>(key.digest());
    }
};

using FramebufferCache = LruCache<NodeCacheKey, std::shared_ptr<Framebuffer>, NodeCacheKeyHash>;

/**
 * @brief Domain-specific caches as recommended by the technical audit.
 */
using PropertyCache    = LruCache<u64, double>; 
using LayerCache       = LruCache<u64, std::shared_ptr<void>>; // Placeholder for evaluated layers
using CompositionCache = LruCache<u64, std::shared_ptr<void>>; // Placeholder for evaluated compositions

class NodeCache {
public:
    using Value = std::shared_ptr<Framebuffer>;

    explicit NodeCache(size_t capacity_bytes = 2048ULL * 1024 * 1024);
    NodeCache(NodeCache&&) noexcept = default;
    NodeCache& operator=(NodeCache&&) noexcept = default;

    [[nodiscard]] Value get(const NodeCacheKey& key);
    void store(const NodeCacheKey& key, Value value);
    
    [[nodiscard]] bool contains(const NodeCacheKey& key) const;
    void clear();
    
    [[nodiscard]] LruCache<NodeCacheKey, Value, NodeCacheKeyHash>::Stats stats() const { return m_cache.stats(); }
    [[nodiscard]] size_t size() const { return m_cache.stats().current_size; }
    
    void set_capacity(size_t capacity_bytes);

    bool erase(const NodeCacheKey& key);

private:
    FramebufferCache m_cache;
};

} // namespace chronon3d::cache
