#pragma once

// ---------------------------------------------------------------------------
// scene_program_store.hpp — Centralized per-session store for compiled
// scene programs, replacing per-PrecompNode SceneProgramCache instances.
//
// PR-5 / PR-2-rewire — Each RenderSession owns exactly one SceneProgramStore.
// PrecompNode instances no longer hold their own SceneProgramCache /
// GraphExecutor / RenderSession / auto-tune counter.  Instead they call
// `ctx.services.session->program_store().acquire(...)` for cache lookup
// and borrow the session's executor for inner execution.  The legacy
// ExecutionPlanCache helper has been RETIRED — topology plans live
// exclusively on CompiledFrameGraph::levels.
//
// Thread-safety: the store's internal map is protected by a mutex;
// per-instance SceneProgramCache instances are already shard-thread-safe.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace chronon3d::graph {

// ── Instance identification ────────────────────────────────────────────────

/// Unique identifier for a single PrecompNode instance within a render
/// session.  Two instances with the same key share the same per-instance
/// cache partition inside the SceneProgramStore.
///
/// Work Package 4 alignment — `graph` is a `GraphInstanceId` (content-
/// derived from the surrounding CompiledFrameGraph's reachable stable
/// node set; FNV-1a over sorted `stable_node_id`s) and `node` is a
/// `StableNodeId` (derived from the precomp node's `layer_id`, `kind`,
/// composition name).  Both fields are kept as `uint64_t` for ABI
/// stability so existing call sites of `PrecompInstanceKey` keep
/// compiling; the alias types in `<chronon3d/render_graph/core/node_identity.hpp>`
/// are exposed for new code that wants the strongly-typed surface.
///
/// NOTE: this struct deliberately stays an AGGREGATE — no user-defined
/// constructor — so that existing designated-initialiser call sites
/// (see `src/render_graph/cache/precomp_node_execute.cpp`) keep
/// compiling.  New producers that want the strong-type surface should
/// call the `make_precomp_key()` helper below.
struct PrecompInstanceKey {
    uint64_t graph{0};
    uint64_t node{0};

    auto operator<=>(const PrecompInstanceKey&) const = default;
};

/// Type-safe factory: build a `PrecompInstanceKey` from the strong
/// types in `<chronon3d/render_graph/core/node_identity.hpp>`.  Call
/// sites that don't pass designated initialisers SHOULD use this
/// helper so the compile-time check catches mismatched-type bugs
/// (`make_precomp_key(StableNodeId, GraphInstanceId)` fails to
/// compile because the strong types refuse to swap positionally).
[[nodiscard]] inline PrecompInstanceKey make_precomp_key(
    GraphInstanceId graph,
    StableNodeId    node
) noexcept {
    return PrecompInstanceKey{
        static_cast<uint64_t>(graph),
        static_cast<uint64_t>(node)
    };
}

} // namespace chronon3d::graph

// std::hash support so PrecompInstanceKey can be used as an unordered_map key.
template <>
struct std::hash<chronon3d::graph::PrecompInstanceKey> {
    std::size_t operator()(const chronon3d::graph::PrecompInstanceKey& k) const noexcept {
        // Combine two uint64_t hashes (same approach as boost::hash_combine).
        std::size_t h = std::hash<uint64_t>{}(k.graph);
        h ^= std::hash<uint64_t>{}(k.node) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

namespace chronon3d::graph {

// ── Cache policy per instance ──────────────────────────────────────────────

/// Per-precomp cache configuration.  Replaces the individual
/// `cache_capacity` / `tune_mode` / `tune_interval` / `tune_min_cap` /
/// `tune_max_cap` ctor params that PrecompNode used to carry.
struct PrecompCachePolicy {
    /// Initial capacity of the per-instance LRU (number of entries).
    std::size_t initial_capacity{8};

    /// Auto-tuning mode (Fixed or Auto).
    cache::TuneMode mode{cache::TuneMode::Fixed};

    /// Auto-tune configuration (only meaningful when mode == Auto).
    cache::TuneConfig tuning{};
};

// ── SceneProgramStore ─────────────────────────────────────────────────────

/// Centralized, session-scoped store of compiled scene programs.
///
/// Owned by RenderSession.  Multiple PrecompNode instances within the same
/// session share this store; each instance is keyed by its PrecompInstanceKey
/// and has its own capacity / tuning policy.
///
/// Usage from PrecompNode::execute():
///   auto lease = ctx.services.session->program_store->acquire(
///       my_instance_key, structure_key, m_cache_policy,
///       [&] { return builder->build(scene, ctx); });
///   if (!lease.program) return empty;
class SceneProgramStore {
public:
    /// Callback that produces a fresh CompiledSceneProgram on cache miss.
    using CompileProgramFn = std::function<std::unique_ptr<CompiledSceneProgram>()>;

    /// Returned by acquire().  Holds a raw pointer to the cached (or
    /// freshly-compiled) program.  The pointer is stable until the next
    /// acquire() for the same instance that triggers an eviction, or until
    /// clear() is called.
    struct ProgramLease {
        CompiledSceneProgram* program{nullptr};
    };

    SceneProgramStore() = default;

    // Non-copyable, non-movable (holds mutex + unique_ptr map).
    SceneProgramStore(const SceneProgramStore&) = delete;
    SceneProgramStore& operator=(const SceneProgramStore&) = delete;

    /// Look up (or compile) a program for the given precomp instance.
    ///
    /// @param owner     Identifies the PrecompNode instance.
    /// @param structure The scene-structure key for cache lookup.
    /// @param policy    Per-instance cache policy (used to create or
    ///                  reconfigure the instance's internal cache).
    /// @param compile   Called on cache miss to produce a fresh program.
    /// @return          Lease with non-null `program` on success.
    ProgramLease acquire(
        PrecompInstanceKey owner,
        const SceneStructureKey& structure,
        const PrecompCachePolicy& policy,
        CompileProgramFn compile
    );

    /// Drop all per-instance caches.  Called when the owning session is
    /// reset (e.g. between unrelated render jobs).
    void clear();

    /// Register an eviction callback for a specific instance.  Fired
    /// whenever the instance's internal cache evicts an entry (LRU
    /// eviction, capacity shrink, or explicit erase).
    void set_on_evict(PrecompInstanceKey owner, cache::ProgramEvictCallback cb);

    // ── Diagnostics ────────────────────────────────────────────────────

    /// Number of active instances in the store.
    [[nodiscard]] std::size_t instance_count() const;

    /// Aggregate stats across all instances.
    [[nodiscard]] cache::SceneProgramCache::Stats aggregate_stats() const;

private:
    /// Per-instance entry: holds a shared_ptr to a SceneProgramCache
    /// so the cache object outlives the map entry.  This protects
    /// against a concurrent clear() while an acquire() is in-flight
    /// (the cache object stays alive via the shared_ptr ref count).
    struct InstanceEntry {
        std::shared_ptr<cache::SceneProgramCache> cache;
        PrecompCachePolicy                         policy;
    };

    /// Find or create the per-instance cache, applying the policy.
    /// Must be called under m_mutex.
    cache::SceneProgramCache& get_or_create_instance(
        PrecompInstanceKey owner,
        const PrecompCachePolicy& policy
    );

    mutable std::mutex m_mutex;
    std::unordered_map<PrecompInstanceKey, InstanceEntry> m_instances;
};

} // namespace chronon3d::graph
