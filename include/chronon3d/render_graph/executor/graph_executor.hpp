#pragma once

// ---------------------------------------------------------------------------
// graph_executor.hpp — Stateless, thread-pool–independent graph executor.
//
// PR-A removed the legacy `execute(RenderGraph&)` overload and the
// `ExecutionPlan`/`CachedExecutionPlan` cache.  PR-B removed the
// `tbb::task_arena m_arena` member and the `pin_main_thread` constructor
// knob.  The executor is now pure compute: input = CompiledFrameGraph &
// RenderGraphContext & RenderSession & ExecutionScheduler & optional
// `FrameArena` override, output = shared_ptr<Framebuffer>.  All mutable
// state lives on either the caller-owned RenderSession (frame arena,
// frame history) or the caller-owned ExecutionScheduler (thread pool).
//
// Construction is cheap (defaulted).  execute() may be called re-entrantly
// from nested graphs (PrecompNode) without coordinating with the parent.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/core/memory/render_session.hpp>
#include <chronon3d/core/memory/arena.hpp>

#include <memory>

namespace chronon3d {

class ExecutionScheduler;  // PR-B: thread-pool authority for the entire engine.

namespace graph {

/// Compiled graph executor (formerly in two flavors — RenderGraph and
/// CompiledFrameGraph — now unified on the compiled path).
///
/// Thread-pool–independent:  The executor holds no
/// `tbb::task_arena` and no mutable member.  All parallelism is delegated
/// to the `ExecutionScheduler&` passed at every `execute()` call.
class GraphExecutor {
public:
    /// Default-constructible.  The executor owns no per-instance state; the
    /// `pin_main_thread` knob moved into `ExecutionScheduler`.
    GraphExecutor() = default;

    /// Execute a compiled frame graph.
    ///
    /// @param compiled        Compiled DAG (levels + consumer counts + output id).
    /// @param ctx             Per-frame context (counters, resources, options).
    /// @param session         RenderSession owning the frame arena + frame history.
    /// @param scheduler       Single authority on thread-pool parallelism for the
    ///                        entire render process (PR-B).  Must outlive this call.
    /// @param arena_override  Optional external arena for short-lived tile-region
    ///                        allocations.  When non-null, the executor uses it
    ///                        instead of `session.arena()`.  Scheduled for removal
    ///                        in PR-E (audit §9.7 — ExecutionScope).
    std::shared_ptr<Framebuffer> execute(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        RenderSession& session,
        ExecutionScheduler& scheduler,
        FrameArena* arena_override = nullptr
    );
};

} // namespace graph
} // namespace chronon3d
