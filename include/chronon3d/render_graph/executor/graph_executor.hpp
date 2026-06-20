#pragma once

// ---------------------------------------------------------------------------
// render_graph/executor/graph_executor.hpp
//
// TICKET-009 — GraphExecutor is now stateless.  All member fields
// (tbb::task_arena m_arena, mutable std::mutex m_plan_mutex,
// CachedExecutionPlan m_cached_plan) and the auxiliary `ExecutionPlan`
// / `CachedExecutionPlan` nested structs have been removed.
//
// What moved:
//   - Plan topology-sort + caching → `chronon3d::runtime::ExecutionPlanCache`
//     (`include/chronon3d/runtime/execution_plan_cache.hpp`).  The executor
//     accepts an optional `runtime::ExecutionPlanCache*` parameter on
//     each `execute()` overload (defaulting to `nullptr`).
//   - `compute_structure_signature`, `build_execution_plan` → also in
//     ExecutionPlanCache as static helpers.
//   - Thread pinning (`pin_main_thread` ctor arg, `m_arena.execute([&]()...)`)
//     → no longer applicable.  Default global TBB thread pool is used
//     for parallel work.
//
// What did NOT move: the executor's runtime orchestration (level
// scheduling, tile pruning, framebuffer lifetime, profiling wiring)
// stays here and is unchanged in semantics.  The behaviour of
// `execute(graph, ctx, session)` returning the same Framebuffer the
// legacy version returned is preserved.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/runtime/execution_plan_cache.hpp>

#include <cstdint>
#include <memory>

namespace chronon3d::graph {

class GraphExecutor {
public:
    /// TICKET-009 — default ctor only.  No pin/arena state owned by the
    /// executor; lifecycle is the caller's responsibility.
    GraphExecutor() = default;

    /// Execute a render graph.
    /// @param session          The RenderSession providing the frame
    ///                         arena and per-frame state.
    /// @param plan_cache       Optional thread-safe plan cache.  Pass
    ///                         `nullptr` to disable plan caching (a
    ///                         fresh plan is built every call); pass a
    ///                         shared instance owned by the caller (e.g.
    ///                         `SoftwareRenderer::plan_cache()` /
    ///                         future `RenderRuntime::plan_cache()`)
    ///                         to enable reuse across `execute()` calls.
    /// @param arena_override   Optional external arena override for
    ///                         temporary allocations.  When provided
    ///                         the executor uses this arena instead of
    ///                         `session.arena()` — used by tile-execution
    ///                         paths that supply a short-lived local
    ///                         arena.
    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        GraphNodeId output,
        RenderGraphContext& ctx,
        RenderSession& session,
        runtime::ExecutionPlanCache* plan_cache = nullptr,
        FrameArena* arena_override = nullptr
    );

    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        RenderGraphContext& ctx,
        RenderSession& session,
        runtime::ExecutionPlanCache* plan_cache = nullptr
    ) {
        return execute(graph, graph.output(), ctx, session, plan_cache);
    }

    std::shared_ptr<Framebuffer> execute(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        RenderSession& session,
        runtime::ExecutionPlanCache* plan_cache = nullptr,
        FrameArena* arena_override = nullptr
    );

    // TICKET-009 — `invalidate_plan_cache()` was removed.  Callers that
    // want to invalidate the cache call
    // `runtime::ExecutionPlanCache::invalidate()` directly on the cache
    // they passed via the parameter list.
};

} // namespace chronon3d::graph
