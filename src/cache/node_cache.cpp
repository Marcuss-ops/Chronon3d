#include <chronon3d/cache/node_cache.hpp>

#define XXH_INLINE_ALL
#include <xxhash.h>
#include <string_view>

namespace chronon3d::cache {

namespace {

[[nodiscard]] u64 hash_string(std::string_view value) {
    return XXH3_64bits(value.data(), value.size());
}

template <typename T>
[[nodiscard]] u64 hash_value(const T& value) {
    return XXH3_64bits(&value, sizeof(T));
}

[[nodiscard]] u64 hash_combine(u64 seed, u64 value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

} // namespace

u64 NodeCacheKey::digest() const {
    u64 seed = hash_string(scope);
    seed = hash_combine(seed, hash_value(frame));
    seed = hash_combine(seed, hash_value(width));
    seed = hash_combine(seed, hash_value(height));
    seed = hash_combine(seed, params_hash);
    seed = hash_combine(seed, source_hash);
    seed = hash_combine(seed, input_hash);
    return seed;
}

NodeCache::NodeCache(size_t capacity_bytes)
    : m_cache(capacity_bytes, capacity_bytes > 64 * 1024 ? 16 : 1) {}

NodeCache::Value NodeCache::get(const NodeCacheKey& key) {
    auto opt = m_cache.get(key);
    return opt ? *opt : nullptr;
}

void NodeCache::store(const NodeCacheKey& key, Value value) {
    if (value) {
        m_cache.put(key, std::move(value), value->size_bytes());
    }
}

bool NodeCache::contains(const NodeCacheKey& key) const {
    return m_cache.contains(key);
}

void NodeCache::clear() {
    m_cache.clear();
}

void NodeCache::set_capacity(size_t capacity_bytes) {
    // Current implementation of LruCache doesn't support dynamic capacity easily
    // without re-sharding. For now, we clear and recreate or just log.
    // In a real scenario, we might want Shard::set_capacity.
    // Let's keep it simple for now.
    clear();
    m_cache = FramebufferCache(capacity_bytes);
}

bool NodeCache::erase(const NodeCacheKey& key) {
    return m_cache.erase(key);
}

} // namespace chronon3d::cache
