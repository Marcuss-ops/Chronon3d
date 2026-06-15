// =============================================================================
// scene_program_cache.cpp — B6: SceneProgramCache implementation
//
/// Implements the count-based LRU cache for CompiledSceneProgram objects.
/// Each shard manages its own lock, entry map, and LRU list.  The find_or_compile
/// template is defined in the header; this file implements the non-template
/// member functions (find, contains, erase, clear, stats, set_capacity).
// =============================================================================

#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cache {

// ── Construction ─────────────────────────────────────────────────────────────

SceneProgramCache::SceneProgramCache(size_t capacity, size_t num_shards)
    : m_capacity(capacity > 0 ? capacity : 1)
    , m_shards(num_shards)
{
    for (auto& shard_ptr : m_shards) {
        shard_ptr = std::make_unique<Shard>();
    }
}

// ── find ─────────────────────────────────────────────────────────────────────

graph::CompiledSceneProgram* SceneProgramCache::find(
    const graph::SceneStructureKey& key)
{
    auto& shard = get_shard(key);
    std::lock_guard<std::mutex> lock(shard.mutex);

    auto it = shard.entries.find(key);
    if (it == shard.entries.end()) {
        return nullptr;
    }

    // Promote to MRU head.
    shard.lru_list.splice(shard.lru_list.begin(), shard.lru_list,
                          it->second.lru_iterator);

    // Record the cache hit for stats and telemetry.
    m_hits.fetch_add(1, std::memory_order_relaxed);
    if (m_counters) {
        m_counters->program_cache_hits.fetch_add(1, std::memory_order_relaxed);
    }

    return it->second.program.get();
}

// ── contains ────────────────────────────────────────────────────────────────

bool SceneProgramCache::contains(const graph::SceneStructureKey& key) const {
    const auto& shard = get_shard(key);
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.entries.contains(key);
}

// ── erase ────────────────────────────────────────────────────────────────────

bool SceneProgramCache::erase(const graph::SceneStructureKey& key) {
    auto& shard = get_shard(key);
    std::lock_guard<std::mutex> lock(shard.mutex);

    auto it = shard.entries.find(key);
    if (it == shard.entries.end()) {
        return false;
    }

    shard.lru_list.erase(it->second.lru_iterator);
    shard.entries.erase(it);

    // Fire the invalidation callback for precomp consumers.
    // Note: does NOT increment m_evictions or telemetry counter —
    // erase is a manual removal, not a capacity-driven eviction.
    // Telemetry counters only track automatic LRU evictions.
    if (m_on_evict) {
        m_on_evict(key);
    }
    return true;
}

// ── clear ────────────────────────────────────────────────────────────────────

void SceneProgramCache::clear() {
    for (auto& shard_ptr : m_shards) {
        std::lock_guard<std::mutex> lock(shard_ptr->mutex);
        shard_ptr->entries.clear();
        shard_ptr->lru_list.clear();
    }
    m_hits.store(0, std::memory_order_relaxed);
    m_misses.store(0, std::memory_order_relaxed);
    m_evictions.store(0, std::memory_order_relaxed);
}

// ── stats ────────────────────────────────────────────────────────────────────

SceneProgramCache::Stats SceneProgramCache::stats() const {
    Stats s;
    s.hits   = m_hits.load(std::memory_order_relaxed);
    s.misses = m_misses.load(std::memory_order_relaxed);
    s.evictions = m_evictions.load(std::memory_order_relaxed);

    size_t total_size = 0;
    for (const auto& shard_ptr : m_shards) {
        std::lock_guard<std::mutex> lock(shard_ptr->mutex);
        total_size += shard_ptr->entries.size();
    }
    s.current_size = total_size;
    return s;
}

// ── size ─────────────────────────────────────────────────────────────────────

size_t SceneProgramCache::size() const {
    size_t total = 0;
    for (const auto& shard_ptr : m_shards) {
        std::lock_guard<std::mutex> lock(shard_ptr->mutex);
        total += shard_ptr->entries.size();
    }
    return total;
}

// ── set_capacity ─────────────────────────────────────────────────────────────

void SceneProgramCache::set_capacity(size_t new_capacity) {
    if (new_capacity == 0) new_capacity = 1;
    m_capacity = new_capacity;

    // Evict excess entries from each shard if the new capacity is tighter.
    const size_t per_shard = new_capacity / m_shards.size();
    for (auto& shard_ptr : m_shards) {
        std::lock_guard<std::mutex> lock(shard_ptr->mutex);
        while (shard_ptr->entries.size() > per_shard &&
               !shard_ptr->lru_list.empty()) {
            auto evicted_key = shard_ptr->lru_list.back();
            shard_ptr->entries.erase(evicted_key);
            shard_ptr->lru_list.pop_back();
            m_evictions.fetch_add(1, std::memory_order_relaxed);
            if (m_counters) {
                m_counters->program_cache_evictions.fetch_add(1, std::memory_order_relaxed);
            }
            if (m_on_evict) {
                m_on_evict(evicted_key);
            }
        }
    }
}

// ── auto_tune ────────────────────────────────────────────────────────────────
//
/// Evaluates internal stats and adjusts capacity if auto-tuning is enabled.
///
/// Algorithm (applied every `m_tune_config.interval` executions):
///   1. Compute hit rate = hits / (hits + misses).
///   2. If evictions > 0 and hit rate < 80% → capacity too small: double it.
///   3. If zero evictions and hit rate > 95% and capacity > min → over-provisioned: halve it.
///   4. Reset counters so the next interval measures fresh stats.
///
/// The decision uses total (all-shards) counters, not per-shard, so it sees
/// a holistic view of the cache pressure.

void SceneProgramCache::auto_tune() {
    if (m_tune_mode != TuneMode::Auto) {
        return;
    }

    const uint64_t total_hits   = m_hits.load(std::memory_order_relaxed);
    const uint64_t total_misses = m_misses.load(std::memory_order_relaxed);
    const uint64_t total_evicts = m_evictions.load(std::memory_order_relaxed);
    const uint64_t total_ops    = total_hits + total_misses;
    size_t new_cap = m_capacity;

    if (total_ops == 0) {
        // No data yet — nothing to tune.
        return;
    }

    const double hit_rate = static_cast<double>(total_hits) / static_cast<double>(total_ops);

    // Condition 1: evictions present and hit rate low → expand (capacity too small).
    if (total_evicts > 0 && hit_rate < 0.80) {
        new_cap = std::min(m_capacity * 2, m_tune_config.max_capacity);
    }
    // Condition 2: evictions present AND high miss rate → expand more aggressively.
    // Only fire when evictions > 0 (proven capacity pressure).  Without evictions
    // the cache has never been full — expanding would not reduce structural misses.
    else if (total_evicts > 0 && total_misses > 0 &&
             static_cast<double>(total_misses) / static_cast<double>(total_ops) > 0.30) {
        new_cap = std::min(m_capacity + m_capacity / 2, m_tune_config.max_capacity);
    }
    // Condition 3: zero evictions and excellent hit rate → maybe shrink.
    else if (total_evicts == 0 && hit_rate >= 0.95 && m_capacity > m_tune_config.min_capacity) {
        new_cap = std::max(m_capacity / 2, m_tune_config.min_capacity);
    }

    // Only call set_capacity if we actually changed the target.
    if (new_cap != m_capacity) {
        const size_t old_cap = m_capacity;
        set_capacity(new_cap);

        // Log the tuning decision so users see live feedback in the terminal.
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

        // Update telemetry counter so per-frame FrameTelemetryRecord captures
        // the evolving capacity (used in the dashboard capacity-trend chart).
        if (m_counters) {
            m_counters->program_cache_capacity.store(new_cap, std::memory_order_relaxed);
        }
    }

    // Reset counters for the next measurement interval.
    m_hits.store(0, std::memory_order_relaxed);
    m_misses.store(0, std::memory_order_relaxed);
    m_evictions.store(0, std::memory_order_relaxed);
}

// ── evict_one_if_needed ──────────────────────────────────────────────────────
//
/// Evicts the single LRU entry if the shard has reached its per-shard capacity.
/// Called from find_or_compile (template, defined in header) before insertion.

void SceneProgramCache::evict_one_if_needed(Shard& shard) {
    // Ceil division so that capacity 3 with 2 shards gives per_shard=2.
    const size_t denom = m_shards.size();
    const size_t per_shard_cap = std::max(size_t(1),
        (m_capacity + denom - 1) / denom);

    while (shard.entries.size() >= per_shard_cap &&
           !shard.lru_list.empty()) {
        auto evicted_key = shard.lru_list.back();
        shard.entries.erase(evicted_key);
        shard.lru_list.pop_back();
        m_evictions.fetch_add(1, std::memory_order_relaxed);

        // Notify telemetry counters
        if (m_counters) {
            m_counters->program_cache_evictions.fetch_add(1, std::memory_order_relaxed);
        }
        // Notify precomp invalidation callback
        if (m_on_evict) {
            m_on_evict(evicted_key);
        }
    }
}

} // namespace chronon3d::cache
