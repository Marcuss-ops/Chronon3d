#pragma once

// ---------------------------------------------------------------------------
// render_graph/executor/graph_executor.hpp
//
// ──────────────────────────────────────────────────────────────────────────
// PRODUCTION CONTRACT (Work Package 2 — Compiled graph only)
// ──────────────────────────────────────────────────────────────────────────
// Production callers MUST use the `CompiledFrameGraph&`
// overload of `execute(...)` defined further down — the two
// `RenderGraph&` overloads that follow are TEST-ONLY.
//
//   FrameGraphCompiler  →  CompiledFrameGraph  →  GraphExecutor::execute
//
// `FrameGraphCompiler` (in
// `<chronon3d/render_graph/compiler/frame_graph_compiler.hpp>`) is the
// sole topology-plan producer for production traffic.  The legacy
// `RenderGraph&` overloads are kept solely so the test suite, the bench
// harness (`tests/bench/bench_scene_program.cpp`) and the bake CLI
// (`apps/chronon3d_cli/commands/render/command_bake_layer.cpp`) keep
// building; new production code MUST NOT call them.  See
// `docs/refactor-roadmap/02-compiled-graph-only.md` for the rationale.
// ──────────────────────────────────────────────────────────────────────────
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
#include <chronon3d/core/scheduler/execution_scheduler.hpp>

#include <cstdint>
#include <memory>

namespace chronon3d::graph {

class GraphExecutor {
public:
    /// TICKET-009 — default ctor only.  No pin/arena state owned by the
    /// executor; lifecycle is the caller's responsibility.
    GraphExecutor() = default;

    // ── TEST-ONLY ENTRYPOINTS (Work Package 2) ──────────────────────────
    // The two overloads below take a raw `RenderGraph&`.  They exist only
    // to keep the test suite, the bench harness and the bake CLI compiling
    // without forcing every caller to install a FrameGraphCompiler
    // shim.  Production code MUST use the `CompiledFrameGraph&`
    // overload and MUST NOT use the raw overloads.

    /// Execute a render graph.  TEST-ONLY — production callers use the
    /// `CompiledFrameGraph&` overload below.
    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        GraphNodeId output,
        RenderGraphContext& ctx,
        RenderSession& session,
        ExecutionScheduler& scheduler,
        runtime::ExecutionPlanCache* plan_cache = nullptr
    ) const;

    /// Execute a render graph.  TEST-ONLY — production callers use the
    /// `CompiledFrameGraph&` overload below.
    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        RenderGraphContext& ctx,
        RenderSession& session,
        ExecutionScheduler& scheduler,
        runtime::ExecutionPlanCache* plan_cache = nullptr
    ) const {
        return execute(graph, graph.output(), ctx, session, scheduler, plan_cache);
    }

    std::shared_ptr<Framebuffer> execute(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        RenderSession& session,
        ExecutionScheduler& scheduler,
        runtime::ExecutionPlanCache* plan_cache = nullptr
    ) const;

    // TICKET-009 — `invalidate_plan_cache()` was removed.  Callers that
    // want to invalidate the cache call
    // `runtime::ExecutionPlanCache::invalidate()` directly on the cache
    // they passed via the parameter list.
};

} // namespace chronon3d::graph
