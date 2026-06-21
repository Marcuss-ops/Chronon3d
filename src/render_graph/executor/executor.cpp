// executor.cpp
// Render graph DAG executor with level-scheduling, cache evaluation,
// dirty-rect clipping, and parallel TBB execution.
//
// TICKET-009 — The executor is now stateless.  No internal arena, no
// internal mutex, no internal plan cache.  Plan caching is supplied
// explicitly via a `runtime::ExecutionPlanCache*` parameter.  Parallel
// work runs on the default global TBB thread pool (no per-instance
// arena).
//
// Internal helpers live in dedicated files under executor/:
//   input_resolver.cpp   — resolve_inputs
//   cache_evaluator.cpp  — evaluate_cache
//   node_runner.cpp      — run_node / execute_single_node
//   telemetry_emitter.cpp— emit_node_records
//   tile_pruning.cpp     — compute_dirty_clip
//   framebuffer_lifetime — init / release FB resources

#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include "executor_levels.hpp"
#include "framebuffer_lifetime.hpp"
#include <chronon3d/render_graph/core/graph_profiler.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chronon3d::graph {

// ──────────────────────────────────────────────────────────────────────
// GraphExecutor public API
// ──────────────────────────────────────────────────────────────────────

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    RenderGraph& graph,
    GraphNodeId output,
    RenderGraphContext& ctx,
    RenderSession& session,
    runtime::ExecutionPlanCache* plan_cache,
    FrameArena* arena_override
) {
    // ── Cached execution plan ───────────────────────────────────────
    // TICKET-009 — the executor is stateless; the cache is supplied via
    // the `plan_cache` parameter.  Three-step lookup:
    //   1. If the caller asserts `graph_structure_unchanged` and the
    //      cache has a valid entry for the requested output, skip the
    //      signature hash (legacy fast-path).
    //   2. Otherwise compute the topology signature and try the cache.
    //   3. On miss, build a fresh plan and store it for next time.
    //
    // When `plan_cache == nullptr` we always build a fresh plan.
    //
    // Hit counts are incremented only when `diagnostics_enabled` is
    // set, matching the legacy behaviour exactly.
    std::shared_ptr<const runtime::ExecutionPlanCache::Plan> plan;
    bool plan_resolved = false;

    if (plan_cache && ctx.policy.graph_structure_unchanged) {
        const auto peek = plan_cache->snapshot();
        if (peek.valid && peek.output == output && peek.plan) {
            plan = peek.plan;
            plan_resolved = true;
            if (ctx.node_exec.counters && ctx.policy.diagnostics_enabled) {
                ctx.node_exec.counters->execution_plan_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (!plan_resolved) {
        const uint64_t sig = runtime::ExecutionPlanCache::compute_structure_signature(graph, output);
        if (plan_cache) {
            plan = plan_cache->try_acquire(sig, output);
            if (plan) {
                plan_resolved = true;
                if (ctx.node_exec.counters && ctx.policy.diagnostics_enabled) {
                    ctx.node_exec.counters->execution_plan_cache_hits.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        if (!plan_resolved) {
            plan = runtime::ExecutionPlanCache::build_execution_plan(graph, output);
            if (plan_cache) {
                plan_cache->store(sig, output, plan);
            }
        }
    }

    if (!plan || plan->levels.empty()) {
        return nullptr;
    }

    FrameArena& active_arena = arena_override ? *arena_override : session.arena();
    auto* res = active_arena.resource();
    struct ArenaGuard {
        FrameArena& arena;
        ~ArenaGuard() { arena.reset(); }
    } guard{active_arena};

    // TICKET-009 — body used to be wrapped in `m_arena.execute([&]() ...)`
    // to scope parallel work onto a dedicated task arena.  With the arena
    // gone the body runs directly on the calling thread; the parallel
    // work inside `execute_levels()` still uses the global TBB pool
    // (functionally equivalent for render-graph workloads).
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
        node_count, plan->consumer_counts, res
    );
    const auto t_fb1 = profiling::now();
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->framebuffer_lifetime_ms.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::duration_ms(t_fb0, t_fb1))),
            std::memory_order_relaxed);
    }

    auto* parent_counters = ctx.node_exec.counters;
    auto* parent_pool = ctx.services.framebuffer_pool.get();

    auto scheduler = make_execution_scheduler(ExecutionSchedulerConfig{});
    execute_levels(graph, ctx, state, scheduler, plan->levels, consumer_remaining, parent_counters, parent_pool, res);

    return state.temp[output];
}

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    RenderSession& session,
    runtime::ExecutionPlanCache* plan_cache,
    FrameArena* arena_override
) {
    auto& graph = compiled.graph;
    const auto& levels = compiled.levels;
    const auto& consumer_counts = compiled.consumer_counts;
    const auto output = compiled.output;

    if (compiled.empty()) {
        return nullptr;
    }

    // Compiled graph already has its plan baked into `levels` — no
    // topology cache lookup needed.  We still honour the `plan_cache`
    // argument for parity, but it is unused in this overload (the
    // compiled graph's plan is the source of truth).
    (void)plan_cache;

    FrameArena& active_arena = arena_override ? *arena_override : session.arena();
    auto* res = active_arena.resource();
    struct ArenaGuard {
        FrameArena& arena;
        ~ArenaGuard() { arena.reset(); }
    } guard{active_arena};

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

    auto scheduler = make_execution_scheduler(ExecutionSchedulerConfig{});
    execute_levels(graph, ctx, state, scheduler, levels, consumer_remaining, parent_counters, parent_pool, res);

    return state.temp[output];
}

} // namespace chronon3d::graph
