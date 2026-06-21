#pragma once

// ---------------------------------------------------------------------------
// runtime/execution_plan_cache.hpp
//
// TICKET-009 — Thread-safe single-slot cache for graph topological-sort
// execution plans, split off from `chronon3d::graph::GraphExecutor` so the
// executor itself can be stateless (no internal mutex, no internal cache).
//
// ──────────────────────────────────────────────────────────────────────────
// Work Package 2 — supplementary status
// ──────────────────────────────────────────────────────────────────────────
// `FrameGraphCompiler` (in
// `<chronon3d/render_graph/compiler/frame_graph_compiler.hpp>`) is the
// SOLE topology-plan producer for production traffic.  This cache is
// `supplementary`: it is consumed only by the retained test-only
// `GraphExecutor::execute(RenderGraph&, ...)` overloads and by the
// SoftwareBackend debug pipeline (`src/render_graph/pipeline/debug.cpp`).
// Production callers MUST use the
// `GraphExecutor::execute(CompiledFrameGraph&, ...)` overload, which
// reads `compiled.levels` directly and does NOT consult this cache; the
// `plan_cache` argument there is a no-op kept only for API parity.
// ──────────────────────────────────────────────────────────────────────────
//
// Construction: shared_ptr-mutable, owned by:
//
//   - SoftwareRenderer (one per renderer instance)
//   - RenderRuntime (planned for TICKET-011; one per Engine lifetime)
//   - test / debug / precomp-node executors that need isolated caches
//
// GraphExecutor::execute() takes an optional `ExecutionPlanCache*` parameter
// (default nullptr). When supplied, the executor consults the cache before
// building a topological plan and writes back the freshly built plan on
// miss. When nullptr (e.g. ad-hoc debug paths), the executor builds a fresh
// plan every call.
//
// The cache is intentionally a single slot (matches the legacy
// `CachedExecutionPlan.valid` boolean semantics).  Going multi-slot is
// reserved for a future optimisation — today the executor touches the cache
// at most once per frame, so lock contention is negligible.
// ===========================================================================

#include <chronon3d/render_graph/render_graph.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace chronon3d::runtime {

class ExecutionPlanCache {
public:
    /// The plan shape (was `GraphExecutor::ExecutionPlan` in the previous
    /// design).  Promoted to public so callers can inspect cached plans
    /// (e.g. telemetry dump).
    struct Plan {
        std::vector<std::vector<chronon3d::graph::GraphNodeId>> levels;
        std::vector<std::size_t> consumer_counts;
    };

    /// Single-slot cache entry.  struct keeps its old shape for backwards
    /// observability (tests, telemetry).
    struct Entry {
        std::uint64_t                            structure_hash{0};
        chronon3d::graph::GraphNodeId            output{chronon3d::graph::k_invalid_node};
        std::shared_ptr<const Plan>              plan;
        bool                                     valid{false};
    };

    /// Try to acquire a cached plan for the given (structure, output) key.
    /// @return nullptr on cache miss; the cached plan on hit.  Hit count
    ///         is bookkeeping-only — callers that want to bump
    ///         `execution_plan_cache_hits` should do so themselves when
    ///         they receive a non-null pointer.
    [[nodiscard]] std::shared_ptr<const Plan> try_acquire(
        std::uint64_t structure_hash,
        chronon3d::graph::GraphNodeId output);

    /// Store a fresh plan for the given (structure, output) key.
    void store(
        std::uint64_t structure_hash,
        chronon3d::graph::GraphNodeId output,
        std::shared_ptr<const Plan> plan);

    /// Drop the cached entry.  Next try_acquire() call against the same
    /// key will miss.
    void invalidate();

    /// Read-only introspection (for telemetry).  Locks the mutex briefly.
    [[nodiscard]] Entry snapshot() const;

    /// Compute a hash that uniquely identifies graph topology:
    /// node kinds, input connectivity, and output node ID.  Moved here
    /// from `GraphExecutor::compute_structure_signature` so the executor
    /// is no longer a friend-of-cache-key.
    [[nodiscard]] static std::uint64_t compute_structure_signature(
        const chronon3d::graph::RenderGraph& graph,
        chronon3d::graph::GraphNodeId output);

    /// Build a topological execution plan from the graph.  Was a private
    /// method of GraphExecutor; promoted so the executor can call it as
    /// a free function on cache misses.
    [[nodiscard]] static std::shared_ptr<const Plan> build_execution_plan(
        chronon3d::graph::RenderGraph& graph,
        chronon3d::graph::GraphNodeId output);

private:
    mutable std::mutex m_mutex;
    Entry m_entry;
};

} // namespace chronon3d::runtime
