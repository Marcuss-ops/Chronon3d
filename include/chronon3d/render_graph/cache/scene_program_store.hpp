#pragma once

// ---------------------------------------------------------------------------
// scene_program_store.hpp — Centralized per-session store for compiled
// scene programs, replacing per-PrecompNode SceneProgramCache instances.
//
// PR-5 / PR-2-rewire — Each RenderSession owns exactly one SceneProgramStore
// (vended through the `runtime::SessionServices::program_store` pointer
// populated by `runtime::make_session()`).  PrecompNode instances no longer
// hold their own SceneProgramCache / GraphExecutor / auto-tune counter.
// Instead they call `ctx.services.session->program_store().acquire(...)`
// for cache lookup and borrow the session's executor for inner execution.
//
// Thread-safety:
//   - The store's internal map is guarded by a mutex for create/clear.
//   - Per-instance SceneProgramCache instances are already shard-thread-safe.
//   - Different precomp-instance buckets execute fully concurrently —
//     each bucket's cache holds its own shard mutex.
//   - No global store lock is held during compilation or rendering.
//
// WP 5.2 — ProgramLease now owns a `std::shared_ptr<CompiledSceneProgram>`
// (instead of the legacy raw pointer) so a concurrent `clear()` on the
// store cannot invalidate an in-flight lease.  The shared_ptr keeps the
// program alive as long as any caller holds a lease, independent of what
// the LRU does with its own references.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace chronon3d::graph {

// ── Instance identification ─────────────────────────────────────────────────

/// Unique identifier for a single PrecompNode instance within a render
/// session.  Two instances with the same key share the same per-instance
/// cache partition inside the SceneProgramStore.
///
/// WP 4.0 — `graph` is a `GraphInstanceId` (content-derived from the
/// surrounding CompiledFrameGraph's reachable stable node set) and
/// `node` is a `StableNodeId`.  Both fields stay as `std::uint64_t` for
/// ABI stability so existing call sites of `PrecompInstanceKey` keep
/// compiling; the strong-type alias surface is exposed in
/// `<chronon3d/render_graph/core/node_identity.hpp>` and used by new
/// producers via `make_precomp_key()`.
///
/// NOTE: this struct deliberately stays an AGGREGATE — no user-defined
/// constructor — so that existing designated-initializer call sites
/// keep compiling.  New producers that want the strong-type surface
/// should call the `make_precomp_key()` helper below.
struct PrecompInstanceKey {
    std::uint64_t graph{0};
    std::uint64_t node{0};

    auto operator<=>(const PrecompInstanceKey&) const = default;
    bool operator==(const PrecompInstanceKey&) const = default;
};

/// Type-safe factory: build a `PrecompInstanceKey` from the strong
/// types in `<chronon3d/render_graph/core/node_identity.hpp>`.  Call
/// sites that don't pass designated initialisers SHOULD use this
/// helper so the compile-time check catches mismatched-type bugs
/// (`make_precomp_key(StableNodeId, GraphInstanceId)` fails to
/// compile because the two strong types refuse to convert to one
/// another positionally).
[[nodiscard]] inline PrecompInstanceKey make_precomp_key(
    GraphInstanceId graph,
    StableNodeId    node
) noexcept {
    return PrecompInstanceKey{
        static_cast<std::uint64_t>(graph.value),
        static_cast<std::uint64_t>(node.value)
    };
}

} // namespace chronon3d::graph

// std::hash support so PrecompInstanceKey can be used as an unordered_map key.
template <>
struct std::hash<chronon3d::graph::PrecompInstanceKey> {
    std::size_t operator()(const chronon3d::graph::PrecompInstanceKey& k) const noexcept {
        // Combine two uint64_t hashes (same approach as boost::hash_combine).
        std::size_t h = std::hash<std::uint64_t>{}(k.graph);
        h ^= std::hash<std::uint64_t>{}(k.node) + 0x9e3779b9 + (h << 6) + (h >> 2);
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
/// Owned by RenderSession (via the `runtime::SessionServices::program_store`
/// pointer populated by `runtime::make_session()`).  Multiple PrecompNode
/// instances within the same session share this store; each instance is
/// keyed by its `PrecompInstanceKey` and has its own capacity / tuning
/// policy.
///
/// Usage from PrecompNode::execute():
///   auto lease = session->program_store().acquire(
///       my_instance_key, structure_key, m_cache_policy,
///       [&] { return builder->build(scene, ctx); });
///   if (!lease.program) return empty;
///
/// WP 5.5 — the store is owned at the runtime layer (see
/// `RenderRuntime::m_owned_program_store`) and vended through
/// `runtime::SessionServices`.  RenderSession::reset_job() drives an
/// isolated `clear()` that touches ONLY the per-instance caches for
/// the owner session (in the single-runtime / single-renderer / single-
/// session production deployment this is the whole store; in any
/// future deployment where multiple SoftwareRenderers share a runtime,
/// the reset reaches across them per the WP-5 close-out — see
/// docs/CHANGELOG.md R5).
class SceneProgramStore {
public:
    /// Callback that produces a fresh CompiledSceneProgram on cache miss.
    using CompileProgramFn = std::function<std::unique_ptr<CompiledSceneProgram>()>;

    /// WP 5.2 — `ProgramLease` is the lifetime/sync handle returned by
    /// `acquire()`.  It holds a `std::shared_ptr` to the cached (or
    /// freshly-compiled) program so the program object stays alive even
    /// if the store's per-instance LRU evicts its entry while the lease
    /// is still in flight (the next acquire() will recompile, but the
    /// currently-in-flight lease keeps its reference alive through the
    /// nested compile/refresh/execute window).  The handle is small,
    /// trivially copyable, and safe to release whenever the caller is
    /// done — the program is destroyed exactly when the last shared_ptr
    /// refcount drops to zero.
    struct ProgramLease {
        std::shared_ptr<CompiledSceneProgram> program{nullptr};
        /// Review #3 — non-owning pointer to the per-instance cache the
        /// program was acquired from.  Lets the caller detect when
        /// `clear()` has replaced the bucket (e.g. after a
        /// `RenderSession::reset_job()`) so it can re-forward any
        /// eviction callback to the new bucket.
        cache::SceneProgramCache* bucket{nullptr};

        [[nodiscard]] CompiledSceneProgram* get() const noexcept {
            return program.get();
        }
        [[nodiscard]] explicit operator bool() const noexcept {
            return program != nullptr;
        }
    };

    SceneProgramStore() = default;

    // Non-copyable, non-movable (holds mutex + unique_ptr map).
    SceneProgramStore(const SceneProgramStore&) = delete;
    SceneProgramStore& operator=(const SceneProgramStore&) = delete;

    /// Look up (or compile) a program for the given precomp instance.
    ///
    /// @param owner     Identifies the PrecompNode instance.
    /// @param structure The scene-structure key for cache lookup.
    /// @param policy    Per-instance cache policy (WP 5.3 — policy is
    ///                  immutable per owner; conflicting policies for the
    ///                  same owner are rejected with a runtime_error
    ///                  rather than silently reconfiguring).
    /// @param compile   Called on cache miss to produce a fresh program.
    /// @return          Lease with non-null `program` on success.
    ///
    /// WP 5.4 — each successful acquire() also drives an internal
    /// per-bucket execution counter; when it crosses
    /// `policy.tuning.interval` the store triggers the bucket's
    /// `auto_tune()` WITHOUT holding any global store lock across the
    /// tuning computation.
    ProgramLease acquire(
        PrecompInstanceKey owner,
        const SceneStructureKey& structure,
        const PrecompCachePolicy& policy,
        CompileProgramFn compile
    );

    /// Drop all per-instance caches.  Called when the owning session is
    /// reset (e.g. between unrelated render jobs).  WP 5.2 — does NOT
    /// invalidate in-flight leases (those keep their shared_ptr refs).
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

    /// WP 5.4 — explicit test/diagnostics hook to invoke the cached
    /// behavior of `record_execution()` + `auto_tune()` on a specific
    /// owner.  Production paths drive this automatically from acquire();
    /// tests can call it directly to assert counter/interval behavior
    /// without going through the full scene-precomp path.
    void record_execution(PrecompInstanceKey owner);

    /// WP 5.4 — total recorded executions across all per-instance
    /// buckets (used to confirm the store wires counter updates from
    /// acquire()).
    [[nodiscard]] std::uint64_t total_recorded_executions() const noexcept;

private:
    /// Per-instance entry.  The map below stores
    /// `std::shared_ptr<InstanceEntry>` so a caller takes ownership of
    /// the bucket pointer for the duration of `acquire()` — a
    /// concurrent `clear()` cannot invalidate the caller's reference
    /// (review #1, #2).
    struct InstanceEntry {
        std::shared_ptr<cache::SceneProgramCache> cache;
        PrecompCachePolicy                         policy;
    };

    /// Find or create the per-instance cache entry.  Hot path holds
    /// `m_mutex` ONLY around the lookup / insertion; cache construction
    /// happens outside the lock.
    std::shared_ptr<InstanceEntry> get_or_create_entry(
        PrecompInstanceKey owner,
        const PrecompCachePolicy& policy
    );

    /// Lookup-only variant used by `set_on_evict`, `record_execution`,
    /// and other lock-free diagnostics.  Returns nullptr if the owner
    /// isn't known.
    std::shared_ptr<InstanceEntry> find_entry(PrecompInstanceKey owner) const;

    /// WP 5.3 — per-owner policy immutability check.  Throws
    /// `std::runtime_error` when a caller supplies a different policy
    /// for an existing owner; silent reconfigure is forbidden.
    static void enforce_immutable_policy(
        const InstanceEntry& existing,
        PrecompInstanceKey owner,
        const PrecompCachePolicy& requested
    );

    mutable std::mutex m_mutex;
    std::unordered_map<PrecompInstanceKey, std::shared_ptr<InstanceEntry>> m_instances;
};

} // namespace chronon3d::graph
