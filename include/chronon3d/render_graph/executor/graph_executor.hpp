#pragma once

// ---------------------------------------------------------------------------
// render_graph/executor/graph_executor.hpp
//
// ──────────────────────────────────────────────────────────────────────────
// PRODUCTION CONTRACT — Compiled graph only
// ──────────────────────────────────────────────────────────────────────────
// The single executor entrypoint is `execute_with_scope(ExecutionScope&, ...)`.
//
//   FrameGraphCompiler  →  CompiledFrameGraph  →  GraphExecutor::execute_with_scope
//
// `FrameGraphCompiler` (in
// `<chronon3d/render_graph/compiler/frame_graph_compiler.hpp>`) is the
// sole topology-plan producer for all traffic.  The historical
// TEST-ONLY `RenderGraph&` overloads were RETIRED (WP-8 Done).
// See `docs/refactor-roadmap/02-compiled-graph-only.md` for the
// exit-criterion trail and the migration path callers followed.
// ---------------------------------------------------------------------------
//
// TICKET-009 — GraphExecutor is stateless.  No internal arena, no
// internal mutex, no internal plan cache.  Plan topology-sort +
// caching lives in `FrameGraphCompiler` (the sole owner of topological
// plans); the supplementary `runtime::ExecutionPlanCache` layer that
// was retained during PR 2.3's transitional phase was retired in the
// PR-2 rewire close-out (see docs/CHANGELOG.md R6).  All callers now
// derive plans from the compiled graph's `levels` directly.
//
// WP-7 — The legacy `execute(RenderSession&, ...)` overload was RETIRED.
// All callers use `execute_with_scope(ExecutionScope&, ...)`, which
// enforces the arena-ownership contract and prevents child teardown
// from invalidating the parent's arena.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>

#include <cstdint>
#include <memory>

namespace chronon3d {
    class RenderSession;
}

namespace chronon3d::graph {

class GraphExecutor {
public:
    /// TICKET-009 — default ctor only.  No pin/arena state owned by the
    /// executor; lifecycle is the caller's responsibility.
    GraphExecutor() = default;

    /// WP-6 / WP-7 — the single production entrypoint.  Takes an
    /// `ExecutionScope&` which owns the identity + arena + parent chain;
    /// the executor reads `scope.session()` and `scope.arena()` directly.
    ///
    /// For non-root scopes (Tile / Precomp), `scope.arena()` is typically
    /// a CHILD arena distinct from the parent's — the
    /// `ArenaGuard reset()` only releases the child arena, leaving
    /// per-job state on the parent session untouched.
    ///
    /// For root scopes, `scope.arena()` is the per-job primary arena
    /// (≡ `session.arena()`).
    ///
    /// If `scope.would_overflow()` returns true, the executor logs the
    /// overflow deterministically via spdlog and returns `nullptr` —
    /// downstream callers interpret null as "engine error / fall back
    /// to empty fb" per docs/03-execution-scope-and-precomp.md §4.4.
    [[nodiscard]] std::shared_ptr<Framebuffer> execute_with_scope(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        ExecutionScope& scope,
        ExecutionScheduler& scheduler
    ) const;
};

} // namespace chronon3d::graph
