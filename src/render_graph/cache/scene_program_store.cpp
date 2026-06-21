// =============================================================================
// scene_program_store.cpp — Centralized SceneProgramStore implementation (PR-5)
// =============================================================================

#include <chronon3d/render_graph/cache/scene_program_store.hpp>

namespace chronon3d::graph {

// ── acquire() ──────────────────────────────────────────────────────────────

SceneProgramStore::ProgramLease SceneProgramStore::acquire(
    PrecompInstanceKey owner,
    const SceneStructureKey& structure,
    const PrecompCachePolicy& policy,
    CompileProgramFn compile)
{
    cache::SceneProgramCache& instance_cache = get_or_create_instance(owner, policy);

    // Delegate to the per-instance SceneProgramCache.  Its find_or_compile()
    // is already thread-safe (sharded mutex).
    auto* program = instance_cache.find_or_compile(structure, std::move(compile));
    return ProgramLease{program};
}

// ── clear() ────────────────────────────────────────────────────────────────

void SceneProgramStore::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_instances.clear();
}

// ── set_on_evict() ─────────────────────────────────────────────────────────

void SceneProgramStore::set_on_evict(
    PrecompInstanceKey owner,
    cache::ProgramEvictCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instances.find(owner);
    if (it != m_instances.end() && it->second.cache) {
        it->second.cache->set_on_evict(std::move(cb));
    }
}

// ── Diagnostics ────────────────────────────────────────────────────────────

std::size_t SceneProgramStore::instance_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_instances.size();
}

cache::SceneProgramCache::Stats SceneProgramStore::aggregate_stats() const {
    cache::SceneProgramCache::Stats total{};
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& [key, entry] : m_instances) {
        if (entry.cache) {
            auto s = entry.cache->stats();
            total.hits         += s.hits;
            total.misses       += s.misses;
            total.evictions    += s.evictions;
            total.current_size += s.current_size;
        }
    }
    return total;
}

// ── get_or_create_instance() ───────────────────────────────────────────────

cache::SceneProgramCache& SceneProgramStore::get_or_create_instance(
    PrecompInstanceKey owner,
    const PrecompCachePolicy& policy)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_instances.find(owner);
    if (it != m_instances.end()) {
        // Existing instance — update policy if it changed.
        // (Currently the policy is set at first acquire; subsequent
        // acquires with different policies update the cache config.)
        auto& entry = it->second;
        if (entry.policy.mode != policy.mode ||
            entry.policy.initial_capacity != policy.initial_capacity ||
            entry.policy.tuning.interval     != policy.tuning.interval ||
            entry.policy.tuning.min_capacity != policy.tuning.min_capacity ||
            entry.policy.tuning.max_capacity != policy.tuning.max_capacity) {
            entry.policy = policy;
            if (entry.cache) {
                entry.cache->set_tune_mode(policy.mode);
                entry.cache->set_tune_config(policy.tuning);
            }
        }
        return *entry.cache;
    }

    // New instance — create a fresh SceneProgramCache with the policy.
    auto cache = std::make_shared<cache::SceneProgramCache>(
        policy.initial_capacity);

    if (policy.mode == cache::TuneMode::Auto) {
        cache->set_tune_mode(cache::TuneMode::Auto);
        cache->set_tune_config(policy.tuning);
    }

    auto& ref = *cache;
    m_instances.emplace(owner, InstanceEntry{
        .cache  = std::move(cache),
        .policy = policy,
    });
    return ref;
}

} // namespace chronon3d::graph
