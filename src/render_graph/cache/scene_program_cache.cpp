// =============================================================================
// scene_program_cache.cpp — B6: SceneProgramCache implementation
//
// Implements the unified compile-program cache.  Storage is delegated to
// chronon3d::cache::LruCache<…, shared_ptr<…>> in CapacityMode::Count.
// This file adds:
//   - capacity resolution (caller-arg > Config/env > kDefaultEntryCap)
//   - bridges for `erase` → user on_evict (LruCache::erase doesn't fire it)
//   - bridges for telemetry counters (in the LruCache.on_evict lambda)
//   - auto-tune loop (reads LruCache::stats())
//   - capacity resize bridge (LruCache::resize emits per-eviction on_evict
//     firing, which the bridge lambda translates into counter updates).
// =============================================================================

#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cache {

// ── Capacity resolution ────────────────────────────────────────────────────

namespace {
constexpr std::size_t kSceneProgramCacheDefaultFallback = 8;

std::size_t resolve_scene_program_cache_max_entries(std::size_t caller_value) {
    if (caller_value > 0) return caller_value;
    const auto v = chronon3d::Config::get().scene_program_cache_max_entries;
    return v > 0 ? v : kSceneProgramCacheDefaultFallback;
}
} // namespace

std::size_t SceneProgramCache::resolve_max_entries(std::size_t caller_value) {
    return resolve_scene_program_cache_max_entries(caller_value);
}

// ── Construction ───────────────────────────────────────────────────────────

SceneProgramCache::SceneProgramCache(
    std::size_t capacity,
    std::size_t num_shards)
    : m_cache(
        /*capacity_weight=*/resolve_max_entries(capacity),
        /*num_shards=*/num_shards,
        /*mode=*/chronon3d::cache::CapacityMode::Count,
        /*on_evict=*/[this](
            const graph::SceneStructureKey& key,
            const std::shared_ptr<graph::CompiledSceneProgram>& /*value*/) {
            // LruCache fires on_evict for every UNCONDITIONAL LRU eviction,
            // AND for every resize-driven eviction.  We translate each into
            // (a) telemetry counter bump, (b) user callback forwarding.
            //
            // NOTE: LruCache::erase() does NOT fire this lambda.  The
            // erase() path is handled in SceneProgramCache::erase() below.
            m_evictions.fetch_add(1, std::memory_order_relaxed);
            if (m_counters) {
                m_counters->program_cache_evictions.fetch_add(1, std::memory_order_relaxed);
            }
            if (m_user_on_evict) {
                m_user_on_evict(key);
            }
        })
    , m_capacity(resolve_max_entries(capacity))
    , m_shard_count(num_shards)
{
}

// ── find ────────────────────────────────────────────────────────────────────

graph::CompiledSceneProgram* SceneProgramCache::find(
    const graph::SceneStructureKey& key)
{
    auto cache_ptr = m_cache.get(key);
    if (!cache_ptr) return nullptr;

    m_hits.fetch_add(1, std::memory_order_relaxed);
    if (m_counters) {
        m_counters->program_cache_hits.fetch_add(1, std::memory_order_relaxed);
    }
    return cache_ptr->get();
}

// ── contains ────────────────────────────────────────────────────────────────

bool SceneProgramCache::contains(const graph::SceneStructureKey& key) const {
    return m_cache.contains(key);
}

// ── erase ───────────────────────────────────────────────────────────────────

bool SceneProgramCache::erase(const graph::SceneStructureKey& key) {
    const bool existed = m_cache.erase(key);
    if (existed && m_user_on_evict) {
        // Preserve the legacy behaviour: explicit erase fires the user
        // callback but does NOT bump m_evictions / telemetry counter
        // (the spec says: "eviction callback fires on erase", but
        // counter bumps are reserved for capacity-driven evictions).
        m_user_on_evict(key);
    }
    return existed;
}

// ── clear ───────────────────────────────────────────────────────────────────

void SceneProgramCache::clear() {
    m_cache.clear();  // LruCache::clear resets its own hits/misses/evictions.
    m_hits.store(0, std::memory_order_relaxed);
    m_misses.store(0, std::memory_order_relaxed);
    m_evictions.store(0, std::memory_order_relaxed);
}

// ── stats ───────────────────────────────────────────────────────────────────

SceneProgramCache::Stats SceneProgramCache::stats() const noexcept {
    Stats s;
    s.hits         = m_hits.load(std::memory_order_relaxed);
    s.misses       = m_misses.load(std::memory_order_relaxed);
    s.evictions    = m_evictions.load(std::memory_order_relaxed);
    s.current_size = m_cache.stats().current_size;
    return s;
}

// ── set_capacity ────────────────────────────────────────────────────────────

void SceneProgramCache::set_capacity(std::size_t new_capacity) {
    if (new_capacity == 0) new_capacity = 1;
    m_capacity = new_capacity;
    // LruCache::resize evicts LRU-tail entries until the per-shard
    // capacity_weight fits, firing on_evict for each.  Our bridge lambda
    // updates m_evictions + telemetry + m_user_on_evict.
    m_cache.resize(new_capacity);

    if (m_counters) {
        m_counters->program_cache_capacity.store(new_capacity, std::memory_order_relaxed);
    }
}

// ── auto_tune ──────────────────────────────────────────────────────────────
//
// Tuning algorithm (preserved from B6 implementation, against the new
// LruCache::stats() read):
//   1. Compute hit rate = hits / (hits + misses).
//   2. If evictions > 0 AND hit_rate < 80%   → capacity too small: double it.
//   3. Else if evictions > 0 AND miss_ratio > 30% → expand by 50%.
//   4. Else if no evictions AND hit_rate >= 95% AND cap > min → halve it.
//   5. Reset counters so the next interval measures fresh data.
//
// Threshold values match B6 spec for backward compatibility with
// tests/render_graph/nodes/test_precomp_node_cache.cpp.

void SceneProgramCache::auto_tune() {
    if (m_tune_mode != TuneMode::Auto) return;

    const std::size_t total_hits   = m_hits.load(std::memory_order_relaxed);
    const std::size_t total_misses = m_misses.load(std::memory_order_relaxed);
    const std::size_t total_evicts = m_evictions.load(std::memory_order_relaxed);
    const std::size_t total_ops    = total_hits + total_misses;
    std::size_t new_cap = m_capacity;

    if (total_ops == 0) {
        return;  // no data yet — nothing to tune.
    }

    const double hit_rate =
        static_cast<double>(total_hits) / static_cast<double>(total_ops);

    // Condition 1: evictions present and hit rate low → expand.
    if (total_evicts > 0 && hit_rate < 0.80) {
        new_cap = std::min(m_capacity * 2, m_tune_config.max_capacity);
    }
    // Condition 2: evictions present AND high miss rate → expand less aggressively.
    else if (total_evicts > 0 && total_misses > 0 &&
             static_cast<double>(total_misses) / static_cast<double>(total_ops) > 0.30) {
        new_cap = std::min(m_capacity + m_capacity / 2, m_tune_config.max_capacity);
    }
    // Condition 3: zero evictions and excellent hit rate → maybe shrink.
    else if (total_evicts == 0 && hit_rate >= 0.95 && m_capacity > m_tune_config.min_capacity) {
        new_cap = std::max(m_capacity / 2, m_tune_config.min_capacity);
    }

    if (new_cap != m_capacity) {
        const std::size_t old_cap = m_capacity;
        set_capacity(new_cap);

        const double hit_rate_pct = hit_rate * 100.0;
        if (!m_log_label.empty()) {
            spdlog::info(
                "{} auto-tuned: {}→{} (evictions={} hit_rate={:.0f}%)",
                m_log_label, old_cap, new_cap, total_evicts, hit_rate_pct);
        } else {
            spdlog::info(
                "SceneProgramCache auto-tuned: {}→{} (evictions={} hit_rate={:.0f}%)",
                old_cap, new_cap, total_evicts, hit_rate_pct);
        }
    }

    // Reset facade counters for the next measurement interval.  Reset the
    // LruCache-side counters via clear()?  No — that drops cached entries.
    // The LruCache's hits/misses/evictions are internal hit/miss/evict
    // counters that we don't read directly (we use the facade ones).
    m_hits.store(0, std::memory_order_relaxed);
    m_misses.store(0, std::memory_order_relaxed);
    m_evictions.store(0, std::memory_order_relaxed);
}

} // namespace chronon3d::cache
