#pragma once

// ---------------------------------------------------------------------------
// render_graph/executor/graph_executor.hpp
//
// ──────────────────────────────────────────────────────────────────────────
// PRODUCTION CONTRACT (Work Package 2 — Compiled graph only)
// ──────────────────────────────────────────────────────────────────────────
// Production callers MUST use the `CompiledFrameGraph&` overload of
// `execute(...)` defined below.  This is the ONLY `execute(...)`
// entrypoint for the executor.
//
//   FrameGraphCompiler  →  CompiledFrameGraph  →  GraphExecutor::execute
//
// `FrameGraphCompiler` (in
// `<chronon3d/render_graph/compiler/frame_graph_compiler.hpp>`) is the
// sole topology-plan producer for all traffic.  The historical
// TEST-ONLY `RenderGraph&` overloads were RETIRED in PR 2.4 / WP-8
// followup — see `docs/refactor-roadmap/02-compiled-graph-only.md` for
// the exit-criterion trail and the migration path callers followed.
// ---------------------------------------------------------------------------
//
// TICKET-009 — GraphExecutor is stateless.  No internal arena, no
// internal mutex, no internal plan cache.  Plan topology-sort +
// caching was extracted into `FrameGraphCompiler` (the sole owner of
// topological plans).  A brief supplementary `runtime::ExecutionPlanCache`
// was retained during the PR 2.3 transitional phase but has now been
// RETIRED alongside the raw-graph overloads; all callers derive plans
// from the compiled graph's `levels` directly.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>

#include <cstdint>
#include <memory>

namespace chronon3d::graph {

class GraphExecutor {
public:
    /// TICKET-009 — default ctor only.  No pin/arena state owned by the
    /// executor; lifecycle is the caller's responsibility.
    GraphExecutor() = default;

    /// Production entrypoint.  Execute a compiled frame graph:
    /// - `compiled.levels` is the source of truth for the topology plan.
    /// - `compiled.output` is the node whose framebuffer is returned.
    ///
    /// Called by tile / single-pass execution paths (production),
    /// the precomp inner executor, the bake CLI (`bake-layer`), and
    /// the test suite (all routed through `FrameGraphCompiler::compile`
    /// before they reach this method).
    [[nodiscard]] std::shared_ptr<Framebuffer> execute(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        RenderSession& session,
        ExecutionScheduler& scheduler
    ) const;
};

} // namespace chronon3d::graph
