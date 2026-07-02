// =============================================================================
// scene_program_store.cpp — Centralized SceneProgramStore implementation
//                             (WP 5.0 / 5.1 / 5.2 / 5.3 / 5.4 / 5.5)
// =============================================================================
//
// WP 5.2 concurrency (review #1, #2) — the map stores
// `std::shared_ptr<InstanceEntry>` so the caller takes ownership of the
// bucket pointer for the duration of the call; a concurrent `clear()`
// cannot invalidate the caller's reference or the per-bucket cache's
// identity.

#include <chronon3d/internal/render_graph/cache/scene_program_store.hpp>

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace chronon3d::graph {

// ── acquire() ──────────────────────────────────────────────────────────────
//
// Concurrency (review #1) — the global `m_mutex` is held ONLY around
// the map mutation in `get_or_create_entry()`.  The hot path
// (record_execution + find_or_compile) runs against the per-bucket
// cache with NO store-level lock held.  Different buckets execute
// fully concurrently.

SceneProgramStore::ProgramLease SceneProgramStore::acquire(
    PrecompInstanceKey owner,
    const SceneStructureKey& structure,
    const PrecompCachePolicy& policy,
    CompileProgramFn compile)
{
    auto entry = get_or_create_entry(owner, policy);

    // WP 5.4 — record the access.  The bucket's atomic counter drives
    // `auto_tune()` on every `interval` calls (Fixed mode is a no-op).
    entry->cache->record_execution();

    // Delegate to the per-instance SceneProgramCache.  Its find_or_compile()
    // is sharded-thread-safe and returns a `shared_ptr<CompiledSceneProgram>`
    // (WP 5.2) so an in-flight lease keeps the program alive across a
    // concurrent `clear()` even when the LRU entry it pointed to is
    // evicted under the bucket.
    auto program = entry->cache->find_or_compile(structure, std::move(compile));
    // Review #3 — always populate lease.bucket (non-owning) so the
    // caller can detect `clear()` cycles by pointer identity when
    // deciding whether to re-forward eviction callbacks.
    return ProgramLease{std::move(program), entry->cache.get()};
}

// ── clear() ────────────────────────────────────────────────────────────────
//
// WP 5.2 — clearing the store drops the per-instance LRU maps; any
// ProgramLease already in flight keeps its shared_ptr alive so the
// executing node completes without dangling.  Concurrent callers
// continue to compile (a fresh cache on the next acquire()).

void SceneProgramStore::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_instances.clear();
}

// ── set_on_evict() ─────────────────────────────────────────────────────────

void SceneProgramStore::set_on_evict(
    PrecompInstanceKey owner,
    cache::ProgramEvictCallback cb)
{
    auto entry = find_entry(owner);
    if (entry && entry->cache) {
        entry->cache->set_on_evict(std::move(cb));
    }
}

// ── record_execution() (diagnostics-exported alias) ────────────────────────

void SceneProgramStore::record_execution(PrecompInstanceKey owner) {
    auto entry = find_entry(owner);
    if (entry && entry->cache) {
        entry->cache->record_execution();
    }
}

// ── Diagnostics ────────────────────────────────────────────────────────────

std::size_t SceneProgramStore::instance_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_instances.size();
}

cache::SceneProgramCache::Stats SceneProgramStore::aggregate_stats() const {
    // Snapshot the bucket shared_ptrs under the lock (cheap), then sum
    // their stats outside the lock so we don't block a concurrent
    // acquire() during the summation read.
    std::vector<std::shared_ptr<InstanceEntry>> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snapshot.reserve(m_instances.size());
        for (const auto& [k, entry] : m_instances) {
            snapshot.push_back(entry);
        }
    }
    cache::SceneProgramCache::Stats total{};
    for (const auto& entry : snapshot) {
        if (entry->cache) {
            auto s = entry->cache->stats();
            total.hits         += s.hits;
            total.misses       += s.misses;
            total.evictions    += s.evictions;
            total.current_size += s.current_size;
        }
    }
    return total;
}

// ── get_or_create_entry() / find_entry() ────────────────────────────────────
//
// The map stores `std::shared_ptr<InstanceEntry>`.  We return a copy to
// the caller so a concurrent `clear()` (or insertion race) cannot
// invalidate the bucket pointer mid-acquire (review #1, #2).

std::shared_ptr<SceneProgramStore::InstanceEntry>
SceneProgramStore::get_or_create_entry(
    PrecompInstanceKey owner,
    const PrecompCachePolicy& policy)
{
    // Fast path: existing entry under the lock.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_instances.find(owner);
        if (it != m_instances.end()) {
            enforce_immutable_policy(*it->second, owner, policy);
            return it->second;
        }
    }

    // Build the new entry outside the lock so the cache constructor
    // (TuneConfig copy, CacheDiagnostics registration) doesn't run
    // under the global mutex.
    auto cache = std::make_shared<cache::SceneProgramCache>(
        policy.initial_capacity);
    if (policy.mode == cache::TuneMode::Auto) {
        cache->set_tune_mode(cache::TuneMode::Auto);
        cache->set_tune_config(policy.tuning);
    }
    auto entry = std::make_shared<InstanceEntry>();
    entry->cache  = std::move(cache);
    entry->policy = policy;

    // Insert under the lock; if another thread raced and inserted first,
    // its value wins and ours is dropped (whose shared_ptr then releases
    // the freshly-allocated cache, which is fine).
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instances.find(owner);
    if (it != m_instances.end()) {
        enforce_immutable_policy(*it->second, owner, policy);
        return it->second;
    }
    m_instances.emplace(owner, entry);
    spdlog::debug("SceneProgramStore: created precomp bucket "
                  "graph={} node={} initial_capacity={} mode={}",
                  owner.graph, owner.node, policy.initial_capacity,
                  static_cast<int>(policy.mode));
    return entry;
}

std::shared_ptr<SceneProgramStore::InstanceEntry>
SceneProgramStore::find_entry(PrecompInstanceKey owner) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instances.find(owner);
    if (it == m_instances.end()) {
        return nullptr;
    }
    return it->second;
}

// ── enforce_immutable_policy() (WP 5.3) ────────────────────────────────────

void SceneProgramStore::enforce_immutable_policy(
    const InstanceEntry& existing,
    PrecompInstanceKey owner,
    const PrecompCachePolicy& requested)
{
    const auto& e = existing.policy;
    const bool same_mode     = e.mode == requested.mode;
    const bool same_initial  = e.initial_capacity == requested.initial_capacity;
    const bool same_interval = e.tuning.interval == requested.tuning.interval;
    const bool same_min      = e.tuning.min_capacity == requested.tuning.min_capacity;
    const bool same_max      = e.tuning.max_capacity == requested.tuning.max_capacity;
    if (!(same_mode && same_initial && same_interval && same_min && same_max)) {
        throw std::runtime_error(
            "SceneProgramStore: conflicting policy for existing owner key "
            "{graph=" + std::to_string(owner.graph)
            + ", node=" + std::to_string(owner.node) + "} "
            "(WP 5.3 — per-owner policy is immutable)");
    }
}

} // namespace chronon3d::graph
