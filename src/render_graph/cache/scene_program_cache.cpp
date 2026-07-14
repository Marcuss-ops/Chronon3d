// =============================================================================
// scene_program_cache.cpp — B6: SceneProgramCache implementation
//
// Implements the unified compile-program cache.  Storage is delegated to
// chronon3d::cache::LruCache<…, shared_ptr<…>> in CapacityMode::Count.
// This file adds:
//   - capacity resolution (caller-arg > Config/env > kDefaultEntryCap)
//   - removal-callback lambda that filters by reason (bumps eviction
//     counters only for Capacity/Resize, always forwards to m_user_on_evict)
//   - auto-tune loop (reads LruCache::stats())
//   - capacity resize bridge (LruCache::resize emits per-eviction removal
//     callbacks with Resize reason).
// =============================================================================

#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cache {

// ── Capacity resolution ────────────────────────────────────────────────────

std::size_t SceneProgramCache::resolve_max_entries(std::size_t caller_value) {
    return resolve_cache_policy(
        CacheDomain::ScenePrograms,
        caller_value > 0 ? std::optional<std::size_t>(caller_value) : std::nullopt
    ).capacity;
}

// ── Construction ───────────────────────────────────────────────────────────

SceneProgramCache::SceneProgramCache(
    std::size_t capacity,
    std::size_t num_shards,
    CacheDiagnostics* diag)
    : m_cache(
        /*capacity_weight=*/resolve_max_entries(capacity),
        /*num_shards=*/num_shards,
        /*mode=*/cache::capacity_mode_for(cache::CacheDomain::ScenePrograms),
        /*on_remove=*/[this](
            const graph::SceneStructureKey& key,
            const std::shared_ptr<graph::CompiledSceneProgram>& /*value*/,
            CacheRemovalReason reason) {
            // LruCache fires on_remove for EVERY entry removal: capacity
            // eviction, resize eviction, erase, clear, and replace.
            // We bump eviction counters only for Capacity/Resize reasons.
            if (reason == CacheRemovalReason::Capacity ||
                reason == CacheRemovalReason::Resize) {
                m_evictions.fetch_add(1, std::memory_order_relaxed);
                if (m_counters) {
                    m_counters->program_cache_evictions.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (m_user_on_evict) {
                m_user_on_evict(key);
            }
        })
    , m_capacity(resolve_max_entries(capacity))
    , m_shard_count(num_shards)
{
    if (diag) {
        m_diag_handle = diag->register_cache(
            CacheDomain::ScenePrograms,
            [this]() -> GenericCacheStats {
                if (!m_diag_alive.load(std::memory_order_acquire)) return {};
                auto s = stats();
                return {s.hits, s.misses, s.evictions, s.current_size,
                        s.current_size /* weight == size in Count mode */};
            },
            [this] {
                if (!m_diag_alive.load(std::memory_order_acquire)) return;
                clear();
            },
            [this] {
                if (!m_diag_alive.load(std::memory_order_acquire)) return CapacityMode::Count;
                return m_cache.capacity_mode();
            },
            m_capacity);
    }
}

void SceneProgramCache::set_diagnostics(CacheDiagnostics& diag) {
    m_diag_alive.store(false, std::memory_order_release);
    m_diag_handle = {};
    m_diag_alive.store(true, std::memory_order_release);
    m_diag_handle = diag.register_cache(
        CacheDomain::ScenePrograms,
        [this]() -> GenericCacheStats {
            if (!m_diag_alive.load(std::memory_order_acquire)) return {};
            auto s = stats();
            return {s.hits, s.misses, s.evictions, s.current_size,
                    s.current_size /* weight == size in Count mode */};
        },
        [this] {
            if (!m_diag_alive.load(std::memory_order_acquire)) return;
            clear();
        },
        [this] {
            if (!m_diag_alive.load(std::memory_order_acquire)) return CapacityMode::Count;
            return m_cache.capacity_mode();
        },
        m_capacity);
}

// ── find ────────────────────────────────────────────────────────────────────

std::shared_ptr<graph::CompiledSceneProgram> SceneProgramCache::find(
    const graph::SceneStructureKey& key)
{
    // LruCache::get returns std::optional<Value>; Value here is
    // std::shared_ptr<CompiledSceneProgram>.  Dereference on hit,
    // nullptr on miss — matching the function's shared_ptr return type.
    auto cache_ptr = m_cache.get(key);
    if (!cache_ptr) return nullptr;

    m_hits.fetch_add(1, std::memory_order_relaxed);
    if (m_counters) {
        m_counters->program_cache_hits.fetch_add(1, std::memory_order_relaxed);
    }
    return *cache_ptr;
}

// ── contains ────────────────────────────────────────────────────────────────

bool SceneProgramCache::contains(const graph::SceneStructureKey& key) const {
    return m_cache.contains(key);
}

// ── erase ───────────────────────────────────────────────────────────────────

bool SceneProgramCache::erase(const graph::SceneStructureKey& key) {
    // LruCache::erase now fires the removal callback with reason=ExplicitErase.
    // Our bridge lambda forwards to m_user_on_evict but does NOT bump
    // eviction counters (it checks the reason and skips for ExplicitErase).
    return m_cache.erase(key);
}

// ── clear ───────────────────────────────────────────────────────────────────

void SceneProgramCache::clear() {
    m_cache.clear();  // LruCache::clear resets its own hits/misses/evictions.
    m_hits.store(0, std::memory_order_relaxed);
    m_misses.store(0, std::memory_order_relaxed);
    m_evictions.store(0, std::memory_order_relaxed);
    m_recorded_executions.store(0, std::memory_order_relaxed);
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
    // capacity_weight fits, firing on_remove for each with Resize reason.
    // Our bridge lambda updates m_evictions + telemetry + m_user_on_evict.
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

// ── record_execution ─────────────────────────────────────────────────────
//
// WP 5.4 — SceneProgramStore calls this every time acquire() succeeds.
// The atomic counter is per-cache; when it crosses
// `m_tune_config.interval`, auto_tune() is invoked synchronously.  In
// `Fixed` mode the function short-circuits without touching the
// counter so the cost is one atomic load + a branch on the hot path.

void SceneProgramCache::record_execution() {
    if (m_tune_mode != TuneMode::Auto) {
        return;
    }
    const std::uint64_t count =
        m_recorded_executions.fetch_add(1, std::memory_order_relaxed) + 1u;
    if (m_tune_config.interval == 0) {
        return;
    }
    if (count % m_tune_config.interval == 0) {
        auto_tune();
    }
}

} // namespace chronon3d::cache
