#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <string_view>

namespace chronon3d::cache {

u64 NodeCacheKey::digest() const {
    return core::hash::HashBuilder{}
        .add(scope)
        .add(frame)
        .add(temporal_key.frame)
        .add(temporal_key.subframe_tick)
        .add(temporal_key.version)
        .add(width)
        .add(height)
        .add(params_hash)
        .add(source_hash)
        .add(input_hash)
        .add(tile_x)
        .add(tile_y)
        .add(tile_size)
        .add(tile_hash)
        .finish();
}

NodeCache::NodeCache(size_t capacity_bytes)
    : m_cache(
          [&] {
              auto p = resolve_cache_policy(CacheDomain::Nodes,
                  capacity_bytes > 0 ? std::optional<std::size_t>(capacity_bytes) : std::nullopt);
              m_diag_handle = CacheDiagnostics::instance().register_cache(
                  CacheDomain::Nodes,
                  [this]() -> GenericCacheStats {
                      if (!m_diag_alive.load(std::memory_order_acquire)) return {};
                      auto s = m_cache.stats();
                      return {s.hits, s.misses, s.evictions, s.current_size, s.current_weight};
                  },
                  [this] {
                      if (!m_diag_alive.load(std::memory_order_acquire)) return;
                      m_cache.clear();
                  },
                  [this] {
                      if (!m_diag_alive.load(std::memory_order_acquire)) return CapacityMode::Weight;
                      return m_cache.capacity_mode();
                  },
                  p.capacity);
              return p;
          }().capacity,
          2,
          capacity_mode_for(CacheDomain::Nodes))
{}

NodeCache::~NodeCache() {
    m_diag_alive.store(false, std::memory_order_release);
}

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
