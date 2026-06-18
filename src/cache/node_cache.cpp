#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <string_view>

namespace chronon3d::cache {

namespace node_cache_detail {

using chronon3d::graph::hash_string;
using chronon3d::graph::hash_combine;

template <typename T>
[[nodiscard]] u64 hash_value(const T& value) {
    return chronon3d::graph::hash_value(value);
}

} // namespace node_cache_detail

u64 NodeCacheKey::digest() const {
    u64 seed = node_cache_detail::hash_string(scope);
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(frame));
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(temporal_key.frame));
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(temporal_key.subframe_tick));
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(temporal_key.version));
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(width));
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(height));
    seed = node_cache_detail::hash_combine(seed, params_hash);
    seed = node_cache_detail::hash_combine(seed, source_hash);
    seed = node_cache_detail::hash_combine(seed, input_hash);
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(tile_x));
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(tile_y));
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(tile_size));
    seed = node_cache_detail::hash_combine(seed, node_cache_detail::hash_value(tile_hash));
    return seed;
}

NodeCache::NodeCache(size_t capacity_bytes)
    : m_cache(
          resolve_cache_policy(CacheDomain::Nodes,
                               capacity_bytes > 0 ? std::optional<std::size_t>(capacity_bytes) : std::nullopt).capacity,
          2,
          capacity_mode_for(CacheDomain::Nodes)) {}

std::shared_ptr<Framebuffer> NodeCache::get(const NodeCacheKey& key) {
    auto val = m_cache.get(key);
    return val.has_value() ? *val : nullptr;
}

void NodeCache::store(const NodeCacheKey& key, Value value) {
    const size_t weight = value ? value->size_bytes() : 0;
    m_cache.put(key, std::move(value), weight);
}

bool NodeCache::contains(const NodeCacheKey& key) const {
    return m_cache.contains(key);
}

void NodeCache::clear() {
    m_cache.clear();
}

void NodeCache::set_capacity(size_t capacity_bytes) {
    if (capacity_bytes > 0) {
        m_cache.resize(capacity_bytes);
    }
}

bool NodeCache::erase(const NodeCacheKey& key) {
    return m_cache.erase(key);
}

} // namespace chronon3d::cache
