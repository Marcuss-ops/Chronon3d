// executor.cpp
// Render graph DAG executor with level-scheduling, cache evaluation,
// dirty-rect clipping, and parallel TBB execution.
//
// TICKET-009 / PR-2 rewire close-out — The executor is now stateless.
// No internal arena, no internal mutex, no internal plan cache.
// Plan topology-sort is owned exclusively by `FrameGraphCompiler` and
// delivered to the executor through `CompiledFrameGraph::levels`.
// Parallel work runs on the default global TBB thread pool (no per-
// instance arena).
//
// Internal helpers live in dedicated files under executor/:
//   input_resolver.cpp   — resolve_inputs
//   cache_evaluator.cpp  — evaluate_cache
//   node_runner.cpp      — run_node / execute_single_node
//   telemetry_emitter.cpp— emit_node_records
//   tile_pruning.cpp     — compute_dirty_clip
//   framebuffer_lifetime — init / release FB resources
//
// PR-2 rewire followup (WP-8) — the two `execute(RenderGraph&, ...)`
// were RETIRED.  All callers compile through `FrameGraphCompiler` first;
// see `docs/refactor-roadmap/02-compiled-graph-only.md`.  // drift-allow: stale-ref

#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/render_backend.hpp>      // P0-1 — NodeExecutionError, render_backend_error_code_name
#include "executor_levels.hpp"
#include "framebuffer_lifetime.hpp"
#include <chronon3d/render_graph/core/graph_profiler.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>      // PR 6.1 — would_overflow() + scope.session()/arena() accessors
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>                                // PR 6.5 — deterministic overflow log
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chronon3d::graph {

// ──────────────────────────────────────────────────────────────────────
// GraphExecutor public API
// ──────────────────────────────────────────────────────────────────────

// PR 6.1 — extracted shared body.  Both legacy `execute(RenderSession&, ...)`
// and new `execute_with_scope(ExecutionScope&, ...)` route through this
// helper; the only difference is which `arena` reference the helper's
// `ArenaGuard` resets on return (parent session's arena vs child scope's
// arena).  The `(session, arena)` signature keeps the helper agnostic to
// whether the caller came via the legacy fragment or the new typed one.
//
// The helper takes `arena` BY REFERENCE because the executor's
// `ArenaGuard` struct needs a stable address for its destructor to fire
// `arena.reset()` deterministically (RAII unwind path).  The lifetime
// invariant is the CALLER's responsibility: `arena` must outlive the
// helper frame.
[[nodiscard]] static std::shared_ptr<Framebuffer> execute_internal(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    RenderSession& session,
    FrameArena& arena,
    ExecutionScheduler& scheduler
) {
    auto& graph = compiled.graph;
    const auto& levels = compiled.levels;
    const auto& consumer_counts = compiled.consumer_counts;
    const auto output = compiled.output;

    if (compiled.empty()) {
        session.publish_last_frame_error(NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            "frame_graph",
            "compiled frame graph is empty"
        });
        return nullptr;
    }

    auto* res = arena.resource();
    struct ArenaGuard {
        FrameArena& arena;
        ~ArenaGuard() { arena.reset(); }
    } guard{arena};

    const size_t node_count = graph.size();
    ExecutionState state(res);
    state.temp.resize(node_count);
    state.resolved_key_digest.assign(node_count, 0);
    state.resolved_frame_dependent.assign(node_count, 0);
    state.resolved_cache_hit.assign(node_count, 0);
    state.resolved_bboxes.resize(node_count);

    const auto t_fb0 = profiling::now();
    init_shared_transparent_fb(state, ctx, res);

    auto consumer_remaining = init_consumer_remaining(
        node_count, consumer_counts, res
    );
    const auto t_fb1 = profiling::now();
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->framebuffer_lifetime_ms.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::duration_ms(t_fb0, t_fb1))),
            std::memory_order_relaxed);
    }

    auto* parent_counters = ctx.node_exec.counters;
    auto* parent_pool = ctx.services.framebuffer_pool.get();

    // P0-1 — seed the shared frame_error slot before dispatching nodes.
    // Nodes that encounter backend failures write into this slot via
    // their cloned RenderGraphContext (shared via clone_for_node_execution).
    ctx.frame_error = std::make_shared<std::optional<NodeExecutionError>>();

    execute_levels(graph, ctx, state, scheduler, levels, consumer_remaining, parent_counters, parent_pool, res, compiled);

    // P0-1 / Fase A5 — after all nodes have executed, check whether any
    // node surfaced a backend failure. The original NodeExecutionError is
    // kept on ctx.frame_error for direct callers and published to the
    // synchronized job-owned slot for CLI/daemon consumers.
    if (ctx.frame_error && ctx.frame_error->has_value()) {
        const auto& err = ctx.frame_error->value();
        session.publish_last_frame_error(err);
        spdlog::error(
            "[executor] frame {} failed: node '{}' error [{}] {}",
            static_cast<int>(ctx.frame_input.frame),
            err.node_name,
            render_backend_error_code_name(err.backend_code),
            err.message);
        return nullptr;
    }

    return state.temp[output];
}

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    RenderSession& session,
    ExecutionScheduler& scheduler
) const {
    return execute_internal(compiled, ctx, session, session.arena(), scheduler);
}

// PR 6.1 — additive overload.  Reads `scope.session()` and `scope.arena()`
// so callers can pass a typed `ExecutionScope&` (with an explicit
// parent-chain + child-arena).
//
// FASE 5 — overflow is no longer possible on the public path: `make_child`
// is the ONLY way to obtain a non-root scope, and it rejects
// `parent->chain_length() >= kMaxScopeChainLength` with
// `ScopeError::ChainLimitExceeded` before the scope is constructed.  The
// previous `if (scope.would_overflow()) { ... return nullptr; }` branch
// has been removed as structural dead code.
std::shared_ptr<Framebuffer> GraphExecutor::execute_with_scope(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    ExecutionScope& scope,
    ExecutionScheduler& scheduler
) const {
    return execute_internal(
        compiled, ctx, scope.session(), scope.arena(), scheduler);
}

} // namespace chronon3d::graph
