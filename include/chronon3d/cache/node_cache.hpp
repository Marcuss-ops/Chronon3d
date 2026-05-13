#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/renderer/framebuffer.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <list>
#include <mutex>

#define XXH_INLINE_ALL
#include <xxhash.h>

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

struct NodeCacheStats {
    usize hits{0};
    usize misses{0};
    usize evictions{0};
    usize current_usage_bytes{0};
};

class NodeCache {
public:
    using Value = std::shared_ptr<Framebuffer>;

    explicit NodeCache(usize capacity_bytes = 1024ULL * 1024 * 1024); // 1GB default

    [[nodiscard]] Value get(u64 key);
    void put(u64 key, Value value, usize size_bytes);
    
    [[nodiscard]] bool contains(u64 key) const;
    void clear();
    
    [[nodiscard]] const NodeCacheStats& stats() const { return m_stats; }
    [[nodiscard]] usize size() const { return m_entries.size(); }
    
    void set_capacity(usize capacity_bytes);

    [[nodiscard]] Value find(const NodeCacheKey& key) { return get(key.digest()); }
    void store(const NodeCacheKey& key, Value value) {
        if (value) put(key.digest(), value, value->size_bytes());
    }

private:
    void evict_if_needed(usize required_bytes);
    void touch(u64 key);

    struct Entry {
        Value value;
        usize size_bytes;
        std::list<u64>::iterator lru_iterator;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<u64, Entry> m_entries;
    std::list<u64> m_lru_list;
    
    usize m_capacity_bytes;
    NodeCacheStats m_stats;
};

} // namespace chronon3d::cache

