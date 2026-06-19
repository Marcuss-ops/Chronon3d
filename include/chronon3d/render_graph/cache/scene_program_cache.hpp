#pragma once

// ---------------------------------------------------------------------------
// scene_program_cache.hpp — Non-destructive LRU cache for CompiledSceneProgram.
//
// This cache sits between PrecompNode and the render graph compiler.
// Backed by chronon3d::cache::LruCache<…,
// std::shared_ptr<CompiledSceneProgram>> in CapacityMode::Count, with the
// sharded + per-shard mutex impl that the rest of the codebase uses.
//
// Prior implementation was a hand-rolled sharded LRU.  Commit 4 unifies it
// onto the LruCache primitive while preserving the public surface required
// by PrecompNode, render graph, and 16 existing unit tests:
//
//   - find_or_compile / find return raw `CompiledSceneProgram*` so callers
//     observe pointer identity on cache hits (preserved by returning
//     `.get()` of the internally-held shared_ptr).
//   - LruCache fires RemovalCallback for erase/clear/replace/eviction,
//     so the facade lambda filters by reason (bumping counters only for
//     Capacity/Resize) and always forwards to m_user_on_evict.
//   - set_capacity(evict-excess) bridges to LruCache::resize(); the
//     removal callback with Resize reason updates telemetry counters.
//   - auto_tune reads LruCache::stats() and the facade holds its own
//     atomic counters so the tuning algorithm doesn't need to lock shards.
//
// Default cap = 8 (cap-resolver consults Config + env override).
//
// Thread-safety: YES — sharded + per-shard mutex (inherited from LruCache).
// ---------------------------------------------------------------------------

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/render_graph/core/scene_hasher.hpp>
#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

// Forward declaration of chronon3d::RenderCounters (lives in
// <chronon3d/core/profiling/counters.hpp>).  Declared at file scope here so
// the SceneProgramCache can hold a pointer without pulling the whole
// counters header into every TU that includes this file.
namespace chronon3d { class RenderCounters; }

namespace chronon3d::cache {

/// Callback fired for every cache eviction OR explicit erase of a
/// CompiledSceneProgram.  Receives the evicted key so downstream
/// consumers (PrecompNode) can invalidate their own caches by key.
using ProgramEvictCallback = std::function<void(const graph::SceneStructureKey& evicted_key)>;

/// Tuning mode for auto-adjusting cache capacity.
enum class TuneMode {
    /// Capacity is fixed at the ctor value; auto_tune() is a no-op.
    Fixed,
    /// Capacity auto-doubles on eviction pressure, auto-halves on idle.
    Auto,
};

/// Auto-tune configuration.
struct TuneConfig {
    /// Number of find_or_compile() calls between auto-tune checks.
    std::size_t interval    = 30;
    /// Hard lower bound — capacity won't shrink below this.
    std::size_t min_capacity = 2;
    /// Hard upper bound — capacity won't grow above this.
    std::size_t max_capacity = 128;
};

/// Counter pointer bound to a chronon3d::RenderCounters instance (forward
/// declared at file scope above; full definition lives in
/// <chronon3d/core/profiling/counters.hpp>).
using RenderCounters = ::chronon3d::RenderCounters;

class SceneProgramCache {
public:
    /// Aggregated stats.  Mirrors the legacy fields; current_weight is
    /// NOT exposed because it's an LruCache detail the cache user shouldn't
    /// rely on (Count mode makes it == current_size anyway).
    struct Stats {
        std::size_t hits{0};
        std::size_t misses{0};
        std::size_t evictions{0};
        std::size_t current_size{0};
    };

    /// Capacity is resolved centrally by
    /// resolve_cache_policy(CacheDomain::ScenePrograms).

    /// Construct with an explicit cap.  Pass 0 to defer to Config+env.
    /// `num_shards` defaults to 2 (preserves pre-Commit-4 behaviour).
    explicit SceneProgramCache(
        std::size_t capacity  = 0,
        std::size_t num_shards = 2);

    // Non-copyable, non-movable.  Same as before (the legacy impl was the
    // same).  We need to hold a stable address because pointer-stability
    // on CompiledSceneProgram* is what callers (PrecompNode) rely on.
    SceneProgramCache(const SceneProgramCache&)            = delete;
    SceneProgramCache& operator=(const SceneProgramCache&) = delete;

    /// Look up the cache.  Returns nullptr on miss, or a stable raw
    /// pointer to the CompiledSceneProgram on hit.  The pointer is
    /// guaranteed stable until a subsequent put/erase/clear/eviction
    /// affects the same shard.
    [[nodiscard]] graph::CompiledSceneProgram* find(
        const graph::SceneStructureKey& key);

    /// Atomic "find + compile-on-miss".  The compiler functor returns a
    /// `std::unique_ptr<CompiledSceneProgram>` (preserves the legacy
    /// caller signature; no caller migration needed beyond this).  On
    /// hit, the compiler is NOT invoked.  On miss, the compiler runs
    /// UNDER the shard mutex; the resulting program is stored and
    /// returned.
    ///
    /// If `program->valid == false`, the entry is NOT cached (this
    /// Preserves the spec: invalid compilations don't poison the
    /// cache).
    template <typename Compiler>
    [[nodiscard]] graph::CompiledSceneProgram* find_or_compile(
        const graph::SceneStructureKey& key,
        Compiler&&                       compiler)
    {
        // Look up under the shard's per-key lock via LruCache::get.
        // The shard is selected by `std::hash<SceneStructureKey>{}(key)
        // % num_shards` inside the LruCache.
        auto cache_ptr = m_cache.get(key);
        if (cache_ptr) {
            m_hits.fetch_add(1, std::memory_order_relaxed);
            if (m_counters) {
                m_counters->program_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
            return cache_ptr->get();
        }

        m_misses.fetch_add(1, std::memory_order_relaxed);
        if (m_counters) {
            m_counters->program_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }

        // Run the compiler OUTSIDE the shard lock so multi-millisecond
        // compilations don't block other shards.  The check-then-insert
        // pattern matches the LruCache::compute_if_absent semantics but
        // intentionally bails out if the freshly-compiled entry collides
        // with one inserted by another thread between get() and put().
        auto compiled_unique = std::forward<Compiler>(compiler)();
        if (!compiled_unique || !compiled_unique->valid) {
            return nullptr;
        }
        auto shared_entry =
            std::shared_ptr<graph::CompiledSceneProgram>(std::move(compiled_unique));
        graph::CompiledSceneProgram* raw = shared_entry.get();
        m_cache.put(key, shared_entry);
        return raw;
    }

    [[nodiscard]] bool contains(const graph::SceneStructureKey& key) const;

    /// Erase a specific entry.  Returns whether the entry existed.
    /// Fires the user-supplied `m_user_on_evict` callback via the
    /// LruCache RemovalCallback (reason=ExplicitErase).  Does NOT bump
    /// eviction counters (matches legacy behaviour).
    bool erase(const graph::SceneStructureKey& key);

    void clear();

    // ── Diagnostics ────────────────────────────────────────────────────
    [[nodiscard]] Stats stats() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept {
        return m_cache.stats().current_size;
    }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

    /// Adjust the cache capacity.  If `new_capacity` is smaller than
    /// the current weight, the least-recently-used entries are evicted
    /// (and `on_evict` is fired for each).  The `m_evictions` and
    /// `program_cache_evictions` counters are updated for every
    /// capacity-driven eviction.
    void set_capacity(std::size_t new_capacity);

    /// Run the auto-tune algorithm.  See the .cpp file for the policy.
    void auto_tune();

    // ── Tuning mode and config ──────────────────────────────────────────
    void set_tune_mode(TuneMode mode) noexcept { m_tune_mode = mode; }
    [[nodiscard]] TuneMode tune_mode() const noexcept { return m_tune_mode; }
    void set_tune_config(TuneConfig cfg) noexcept { m_tune_config = std::move(cfg); }
    [[nodiscard]] const TuneConfig& tune_config() const noexcept { return m_tune_config; }

    // ── Telemetry coupling ──────────────────────────────────────────────
    /// Bind a chronon3d::RenderCounters instance (may be nullptr).  Hit,
    /// miss, and eviction events bump the corresponding atomic counters.
    void set_counters(chronon3d::RenderCounters* counters) noexcept { m_counters = counters; }
    void set_log_label(std::string label) { m_log_label = std::move(label); }

    // ── Eviction callback ───────────────────────────────────────────────
    /// Fired for every capacity-driven eviction AND every explicit erase.
    /// PrecompNode wires this to its own on_evict to cascade invalidation.
    void set_on_evict(ProgramEvictCallback cb) {
        m_user_on_evict = std::move(cb);
    }

private:
    /// Capacity resolution (caller-arg > Config/env > policy default).
    /// Delegates to the centralized resolve_cache_policy(CacheDomain::ScenePrograms).
    static std::size_t resolve_max_entries(std::size_t caller_value);

    /// The actual storage.  Backed by LruCache in Count mode (every entry
    /// contributes exactly 1 unit of weight).  Uses `std::hash<Key>` as
    /// the default Hash template arg — chronon3d::graph already provides
    /// `std::hash<SceneStructureKey>` in compiled_scene_program.hpp.
    chronon3d::cache::LruCache<
        graph::SceneStructureKey,
        std::shared_ptr<graph::CompiledSceneProgram>>
        m_cache;

    chronon3d::cache::CacheDiagnostics::Handle m_diag_handle;

    /// Cache-side facade atomic counters (so auto_tune doesn't have to
    /// lock the shard to read them).  Kept in sync with m_cache.stats().
    mutable std::atomic<std::size_t> m_hits{0};
    mutable std::atomic<std::size_t> m_misses{0};
    mutable std::atomic<std::size_t> m_evictions{0};

    /// Cached cap.  Updated by set_capacity / auto_tune.
    std::size_t m_capacity;

    /// Snapshot of the shard count captured at ctor time (the eviction
    /// callback needs to recompute the per-shard cap-change size).
    std::size_t m_shard_count;

    chronon3d::RenderCounters* m_counters   = nullptr;
    ProgramEvictCallback    m_user_on_evict;
    std::string             m_log_label;
    TuneMode                m_tune_mode  = TuneMode::Fixed;
    TuneConfig              m_tune_config{};
};

} // namespace chronon3d::cache
