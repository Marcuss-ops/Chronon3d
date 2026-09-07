#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace chronon3d::cache {

// OPT-SHARD-COUNT — the per-frame node-cache lookups of all parallel node
// workers used to funnel into 2 shards; with typical level widths the shard
// mutex was the next contention point after the framebuffer pool. More
// shards cut same-shard collisions ~4x on 8+ core renders.
constexpr std::size_t kNodeCacheMaxShards = 8;
// A shard must not slice the budget below a plausibly-cacheable single node
// output. The framebuffer-pool watermark (~64 MiB default) bounds typical
// node-output weight, so every shard gets at least that much; with the 2 GiB
// default budget that lands on the 8-way split. Shrink budgets (config, or a
// runtime set_capacity below the default) reduce the shard count instead of
// refusing every put as "oversized" — a single item within TOTAL capacity
// must remain cacheable (contract: tests/cache/test_cache_invariance.cpp).
constexpr std::size_t kNodeCacheMinShardCapacity = 64ULL * 1024 * 1024;
constexpr std::size_t node_cache_shard_count(std::size_t capacity_bytes) noexcept {
    const std::size_t by_capacity =
        capacity_bytes / kNodeCacheMinShardCapacity;
    return std::clamp<std::size_t>(by_capacity, 1, kNodeCacheMaxShards);
}

/// The shard count must derive from the capacity the policy actually grants
/// (it may clamp/round the request), so both ctor init arguments resolve the
/// policy through this pure helper.
std::size_t resolved_nodes_capacity(std::size_t requested) {
    const auto p = resolve_cache_policy(CacheDomain::Nodes,
        requested > 0 ? std::optional<std::size_t>(requested) : std::nullopt);
    return p.capacity;
}

u64 NodeCacheKey::digest() const {
    // HOT-PATH TAX D — finalized keys read the memo; everything else falls
    // back to the pure computation so semantics never change.
    return digest_finalized ? precomputed_digest : compute_digest();
}

u64 NodeCacheKey::compute_digest() const {
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
        .add(params_version)
        .add(source_version)
        .add(input_version)
        .add(tile_x)
        .add(tile_y)
        .add(tile_size)
        .add(tile_hash)
        .finish();
}

NodeCache::NodeCache(size_t capacity_bytes, CacheDiagnostics* diag)
    : m_cache(
          [&] {
              auto p = resolve_cache_policy(CacheDomain::Nodes,
                  capacity_bytes > 0 ? std::optional<std::size_t>(capacity_bytes) : std::nullopt);
              if (diag) {
                  m_diag_handle = diag->register_cache(
                      CacheDomain::Nodes,
                      [this]() -> GenericCacheStats {
                          if (!m_diag_alive.load(std::memory_order_acquire)) return {};
                          return make_generic_cache_stats(m_cache.stats());
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
              }
              // Shard count derives from the RESOLVED policy capacity (not the
              // raw ctor argument): the policy can clamp/round the requested
              // budget, and shards must never overslice the effective budget.
              return p;
          }().capacity,
          node_cache_shard_count(resolved_nodes_capacity(capacity_bytes)),
          capacity_mode_for(CacheDomain::Nodes))
{}

NodeCache::~NodeCache() {
    m_diag_alive.store(false, std::memory_order_release);
}

void NodeCache::set_diagnostics(CacheDiagnostics& diag) {
    m_diag_alive.store(false, std::memory_order_release);
    m_diag_handle = {};  // RAII-drop old handle (no-op if was default-constructed)
    m_diag_alive.store(true, std::memory_order_release);
    m_diag_handle = diag.register_cache(
        CacheDomain::Nodes,
        [this]() -> GenericCacheStats {
            if (!m_diag_alive.load(std::memory_order_acquire)) return {};
            return make_generic_cache_stats(m_cache.stats());
        },
        [this] {
            if (!m_diag_alive.load(std::memory_order_acquire)) return;
            m_cache.clear();
        },
        [this] {
            if (!m_diag_alive.load(std::memory_order_acquire)) return CapacityMode::Weight;
            return m_cache.capacity_mode();
        },
        resolve_cache_policy(CacheDomain::Nodes, std::nullopt).capacity);
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
        m_cache.resize(capacity_bytes,
            node_cache_shard_count(resolved_nodes_capacity(capacity_bytes)));
    }
}

bool NodeCache::erase(const NodeCacheKey& key) {
    return m_cache.erase(key);
}

} // namespace chronon3d::cache