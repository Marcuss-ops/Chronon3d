// ---------------------------------------------------------------------------
// executor.cpp — Compiled-frame-graph executor
//
// PR-A + PR-B refactor — stateless executor with no mutable members and no
// tbb::task_arena.  Schedule authority is delegated to the ExecutionScheduler
// passed in by the caller (SoftwareRenderer keeps the singleton scheduler
// alive in m_runtime_resources for the lifetime of the renderer).
//
// Internal helpers live in dedicated files under executor/:
//   input_resolver.cpp   — resolve_inputs
//   cache_evaluator.cpp  — evaluate_cache
//   node_runner.cpp      — run_node / execute_single_node
//   telemetry_emitter.cpp— emit_node_records
//   tile_pruning.cpp     — compute_dirty_clip
//   framebuffer_lifetime — init / release FB resources
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "executor_levels.hpp"
#include "framebuffer_lifetime.hpp"
#include <chronon3d/render_graph/core/graph_profiler.hpp>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    RenderSession& session,
    ExecutionScheduler& scheduler,
    FrameArena* arena_override
) {
    auto& graph = compiled.graph;
    const auto& levels = compiled.levels;
    const auto& consumer_counts = compiled.consumer_counts;
    const auto output = compiled.output;

    if (compiled.empty()) {
        return nullptr;
    }

    FrameArena& active_arena = arena_override ? *arena_override : session.arena();
    auto* res = active_arena.resource();
    struct ArenaGuard {
        FrameArena& arena;
        ~ArenaGuard() { arena.reset(); }
    } guard{active_arena};

    return scheduler.run([&]() -> std::shared_ptr<Framebuffer> {
        const size_t node_count = graph.size();
        ExecutionState state(res);
        state.temp.resize(node_count);
        state.resolved_key_digest.assign(node_count, 0);
        state.resolved_frame_dependent.assign(node_count, 0);
        state.resolved_cache_hit.assign(node_count, 0);
        state.resolved_bboxes.resize(node_count);

        // ── Pre-allocate shared transparent framebuffer for tile pruning ──
        const auto t_fb0 = profiling::now();
        init_shared_transparent_fb(state, ctx, res);

        auto consumer_remaining = init_consumer_remaining(
            node_count, consumer_counts, res
        );
        const auto t_fb1 = profiling::now();
        if (ctx.telemetry.counters) {
            ctx.telemetry.counters->framebuffer_lifetime_ms.fetch_add(
                static_cast<uint64_t>(std::llround(profiling::duration_ms(t_fb0, t_fb1))),
                std::memory_order_relaxed);
        }

        auto* parent_counters = ctx.telemetry.counters;
        auto* parent_pool = ctx.resources.framebuffer_pool.get();

        // ── PR-B: telemetry reads scheduler.concurrency() ──────────────
        // Replaces the legacy `std::thread::hardware_concurrency()`
        // snapshot so that Sequential mode reports 1, TbbFixed reports N,
        // TbbAutomatic reports whatever the runtime arena resolves to.
        if (parent_counters) {
            parent_counters->tbb_arena_max_concurrency.store(
                static_cast<uint64_t>(scheduler.concurrency()),
                std::memory_order_relaxed);
        }

        execute_levels(graph, ctx, state, scheduler, levels, consumer_remaining,
                       parent_counters, parent_pool, res);

        return state.temp[output];
    });
}

} // namespace chronon3d::graph
