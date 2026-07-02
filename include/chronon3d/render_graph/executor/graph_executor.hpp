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
// caching lives in `FrameGraphCompiler` (the sole owner of topological
// plans); the supplementary `runtime::ExecutionPlanCache` layer that
// was retained during PR 2.3's transitional phase was retired in the
// PR-2 rewire close-out (see docs/CHANGELOG.md R6).  All callers now
// derive plans from the compiled graph's `levels` directly.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>  // PR 6.1 — reference parameter for execute_with_scope(...)

#include <cstdint>
#include <memory>

namespace chronon3d::graph {

class GraphExecutor {
public:
    /// TICKET-009 — default ctor only.  No pin/arena state owned by the
    /// executor; lifecycle is the caller's responsibility.
    GraphExecutor() = default;

    /// Legacy entrypoint (PR 6.0 / pre-PR-6.1 contract).  Execute a
    /// compiled frame graph using `session.arena()` as the allocation
    /// surface.  Kept ADDITIVE alongside `execute_with_scope(...)` so
    /// existing test lattice + tile fallback paths continue to compile
    /// unmodified — WP-7 will retire this overload once all production
    /// call sites are migrated to `execute_with_scope(...)`.
    ///
    /// PR 6.7 — DEPRECATED.  Callers MUST migrate to the typed
    /// `execute_with_scope(ExecutionScope&, ...)` overload which
    /// enforces the arena-ownership contract and prevents child teardown
    /// from invalidating the parent's arena.  This overload will be
    /// REMOVED in WP-7 once all call sites (production + test lattice)
    /// pass an explicit `ExecutionScope&`.
    ///
    /// - `compiled.levels` is the source of truth for the topology plan.
    /// - `compiled.output` is the node whose framebuffer is returned.
    [[deprecated("use execute_with_scope(ExecutionScope&, ExecutionScheduler&) — the session-based path will be removed in WP-7; pass a typed scope to enforce arena-ownership isolation")]]
    [[nodiscard]] std::shared_ptr<Framebuffer> execute(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        RenderSession& session,
        ExecutionScheduler& scheduler
    ) const;

    /// PR 6.1 — new production entrypoint, takes an `ExecutionScope&`
    /// instead of `RenderSession&`.  The scope owns the identity + arena
    /// + parent chain; the executor reads `scope.session()` and
    /// `scope.arena()` directly.  PrecompNode inner execution + tile
    /// orchestration + the bake CLI will route through this method once
    /// those call sites are migrated (PR 6.2 / 6.3 / 6.4).
    ///
    /// For non-root scopes (Tile / Precomp), `scope.arena()` is typically
    /// a CHILD arena distinct from the parent's — the
    /// `ArenaGuard reset()` only releases the child arena, leaving
    /// per-job state on the parent session untouched.  This is the PR 6.4
    /// isolation contract in code form.
    ///
    /// For root scopes, `scope.arena()` is the per-job primary arena
    /// (≡ `session.arena()`) and the behaviour is bit-equal to the
    /// legacy `execute(RenderSession&, ...)` overload.
    ///
    /// If `execute_with_scope` returns `nullptr`, downstream callers
    /// (`tile_execution_coordinator.cpp`, `precomp_node_execute.cpp`)
    /// interpret null as the documented "engine error / fall back to
    /// empty fb" per docs/03-§4.4.
    ///
    /// FASE 5 — overflow is no longer possible on the public path.
    /// `scope` MUST have been constructed via
    /// `ExecutionScope::make_root()` or `ExecutionScope::make_child()`,
    /// both of which enforce the chain-length and arena-aliasing
    /// invariants documented in `core/scope/execution_scope.hpp`.
    /// Callers that bypass these factories obtain undefined behavior at
    /// link time — the 5-arg explicit ctor is `private` post-FASE-5.
    [[nodiscard]] std::shared_ptr<Framebuffer> execute_with_scope(
        CompiledFrameGraph& compiled,
        RenderGraphContext& ctx,
        ExecutionScope& scope,
        ExecutionScheduler& scheduler
    ) const;
};

} // namespace chronon3d::graph
