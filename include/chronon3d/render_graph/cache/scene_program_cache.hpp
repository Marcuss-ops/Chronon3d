#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// scene_program_cache.hpp — B6: LRU cache for CompiledSceneProgram
//
// A count-based LRU cache that stores compiled scene programs keyed by
// SceneStructureKey.  Provides a find_or_compile() API that returns a stable
// pointer to the cached program — the cache is non-destructive on read
// (accessing an entry promotes it in the LRU order but never invalidates it).
//
// Supports two optional integrations:
//   1. Telemetry counters — via set_counters(), increments program_cache_hits,
//      program_cache_misses, and program_cache_evictions on the given
//      RenderCounters instance.
//   2. Precomp invalidation — via set_on_evict(), a callback invoked whenever
//      an entry is evicted from the cache.  The callback receives the evicted
//      program's SceneStructureKey so that PrecompNode (or other consumers)
//      can invalidate their cached state.
//
// Thread-safety: Sharded locking (2 shards by default) to reduce contention.
// The cache owns the CompiledSceneProgram objects; returned pointers remain
// valid until eviction or cache clear.
//
// Eviction policy (from spec §B6): when inserting beyond capacity, the
// least-recently-used entry (LRU tail) is evicted.  Accessing an entry
// promotes it to the MRU (head) position.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>
#include <atomic>
#include <cstdint>
#include <string>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

// Forward declaration for telemetry counters.
namespace chronon3d {
struct RenderCounters;
}

namespace chronon3d::cache {

// ── Eviction callback type ───────────────────────────────────────────────────
/// Invoked when an entry is evicted from the cache.  Receives the key of the
/// evicted entry so consumers (e.g. PrecompNode) can invalidate derived state.
using ProgramEvictCallback = std::function<void(const graph::SceneStructureKey& evicted_key)>;

// ── Tune mode ────────────────────────────────────────────────────────────────
/// Controls whether the cache automatically adjusts its capacity based on
/// hit/eviction statistics observed during execution.
enum class TuneMode {
    /// Manual capacity only — no automatic adjustment.
    Fixed,
    /// Automatically tune capacity: double when evictions are frequent and
    /// hit rate is low, halve when fully saturated with zero evictions.
    Auto
};

// ── TuneConfig ───────────────────────────────────────────────────────────────
/// Configuration for automatic capacity tuning.
struct TuneConfig {
    /// How many find_or_compile() calls between auto-tune checks.
    size_t interval{30};
    /// Minimum capacity after down-tuning.
    size_t min_capacity{2};
    /// Maximum capacity after up-tuning.
    size_t max_capacity{128};
};

// ── SceneProgramCache ────────────────────────────────────────────────────────

/// A non-destructive LRU cache for CompiledSceneProgram objects.
/// Capacity is measured in number of entries (not bytes).
class SceneProgramCache {
public:
    /// Stats for diagnostics and telemetry.
    struct Stats {
        uint64_t hits{0};
        uint64_t misses{0};
        uint64_t evictions{0};
        size_t   current_size{0};
    };

    /// Create a cache with `capacity` entries (default 8) and `num_shards`
    /// shards for reduced lock contention.
    explicit SceneProgramCache(size_t capacity = 8, size_t num_shards = 2);

    /// Not copyable or movable (returned pointers would dangle).
    SceneProgramCache(const SceneProgramCache&) = delete;
    SceneProgramCache& operator=(const SceneProgramCache&) = delete;

    /// Attach telemetry counters. The cache will increment
    /// program_cache_hits / program_cache_misses / program_cache_evictions.
    void set_counters(chronon3d::RenderCounters* counters) { m_counters = counters; }

    /// Attach an eviction callback, invoked for every evicted entry.
    /// Used by PrecompNode to invalidate cached nested renders.
    void set_on_evict(ProgramEvictCallback cb) { m_on_evict = std::move(cb); }

    /// Set a human-readable label for log messages (e.g. "Precomp:MyComposition").
    void set_log_label(std::string label) { m_log_label = std::move(label); }

    /// Find or compile a program for the given key.
    ///
    /// If the key is present in the cache, the cached program is returned
    /// (cache hit) and promoted to the MRU position.
    ///
    /// If the key is absent, the `compiler` function is called (cache miss)
    /// and the resulting program is stored.  If the cache is at capacity,
    /// the LRU entry is evicted first.
    ///
    /// The compiler function signature:
    ///   std::unique_ptr<CompiledSceneProgram>() -> return compiled program
    ///
    /// Returns a stable pointer to the cached program.  The pointer remains
    /// valid until the entry is evicted or clear() is called.
    template <typename Compiler>
    graph::CompiledSceneProgram* find_or_compile(
        const graph::SceneStructureKey& key,
        Compiler&& compiler)
    {
        auto& shard = get_shard(key);
        std::lock_guard<std::mutex> lock(shard.mutex);

        auto it = shard.entries.find(key);
        if (it != shard.entries.end()) {
            // Cache hit — promote to MRU head.
            shard.lru_list.splice(shard.lru_list.begin(), shard.lru_list,
                                  it->second.lru_iterator);
            m_hits.fetch_add(1, std::memory_order_relaxed);
            if (m_counters) {
                m_counters->program_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
            return it->second.program.get();
        }

        // Cache miss — compile and store.
        m_misses.fetch_add(1, std::memory_order_relaxed);
        if (m_counters) {
            m_counters->program_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        auto program = compiler();
        if (!program || !program->valid) {
            return nullptr;
        }

        // Evict LRU if at capacity.
        evict_one_if_needed(shard);

        // Insert at MRU head.
        shard.lru_list.push_front(key);
        auto* raw_ptr = program.get();
        shard.entries[key] = Entry{std::move(program), shard.lru_list.begin()};

        return raw_ptr;
    }

    /// Look up a key without compiling.  Returns nullptr if absent.
    /// Promotes the entry to MRU if found.
    [[nodiscard]] graph::CompiledSceneProgram* find(
        const graph::SceneStructureKey& key);

    /// Check if a key exists without promoting or locking.
    [[nodiscard]] bool contains(const graph::SceneStructureKey& key) const;

    /// Erase a specific entry.  Returns true if the entry was present.
    bool erase(const graph::SceneStructureKey& key);

    /// Clear all entries.
    void clear();

    /// Return current stats (aggregated across shards).
    [[nodiscard]] Stats stats() const;

    /// Return current number of entries (aggregated across shards).
    [[nodiscard]] size_t size() const;

    /// Return the configured capacity.
    [[nodiscard]] size_t capacity() const { return m_capacity; }

    /// Resize capacity.  If the new capacity is smaller than the current
    /// size, excess LRU entries are evicted.
    void set_capacity(size_t new_capacity);

    // ── Auto-tuning ───────────────────────────────────────────────────────

    /// Enable or disable automatic capacity tuning.
    void set_tune_mode(TuneMode mode) { m_tune_mode = mode; }

    /// Return the current tune mode.
    [[nodiscard]] TuneMode tune_mode() const { return m_tune_mode; }

    /// Configure tuning parameters (interval, min/max capacity).
    void set_tune_config(const TuneConfig& config) { m_tune_config = config; }

    /// Return the current tuning configuration.
    [[nodiscard]] const TuneConfig& tune_config() const { return m_tune_config; }

    /// Evaluate current stats and adjust capacity if auto-tuning is enabled.
    /// Resets internal hit/miss/eviction counters after the decision so the
    /// next auto-tune interval measures fresh statistics.
    void auto_tune();

private:    /// Hash functor for SceneStructureKey.
    struct KeyHash {
        size_t operator()(const graph::SceneStructureKey& key) const noexcept {
            size_t h = 1469598103934665603ULL;  // FNV-1a offset basis
            auto combine = [&](uint64_t v) {
                h ^= static_cast<size_t>(v);
                h *= 1099511628211ULL;
            };
            combine(key.topology_hash);
            combine(key.active_set_hash);
            combine(key.render_options_hash);
            combine(static_cast<uint64_t>(key.width));
            combine(static_cast<uint64_t>(key.height));
            combine(static_cast<uint64_t>(key.ssaa_factor));
            return h;
        }
    };

    struct Entry {
        std::unique_ptr<graph::CompiledSceneProgram> program;
        std::list<graph::SceneStructureKey>::iterator lru_iterator;
    };

    struct Shard {
        mutable std::mutex mutex;
        std::unordered_map<
            graph::SceneStructureKey,
            Entry,
            KeyHash
        > entries;
        std::list<graph::SceneStructureKey> lru_list;
    };

    Shard& get_shard(const graph::SceneStructureKey& key) {
        KeyHash hasher;
        return *m_shards[hasher(key) % m_shards.size()];
    }

    const Shard& get_shard(const graph::SceneStructureKey& key) const {
        KeyHash hasher;
        return *m_shards[hasher(key) % m_shards.size()];
    }

    void evict_one_if_needed(Shard& shard);

    size_t m_capacity;
    std::vector<std::unique_ptr<Shard>> m_shards;
    mutable std::atomic<uint64_t> m_hits{0};
    mutable std::atomic<uint64_t> m_misses{0};
    mutable std::atomic<uint64_t> m_evictions{0};

    // ── Optional integrations ───────────────────────────────────────────
    chronon3d::RenderCounters* m_counters{nullptr};
    ProgramEvictCallback m_on_evict;

    // ── Auto-tuning state ───────────────────────────────────────────────
    TuneMode m_tune_mode{TuneMode::Fixed};
    TuneConfig m_tune_config{};

    /// Optional label for log messages (e.g. "Precomp:MyComposition").
    std::string m_log_label;
};

} // namespace chronon3d::cache
