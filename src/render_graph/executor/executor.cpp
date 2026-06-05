// executor.cpp
// Render graph DAG executor with level-scheduling, cache evaluation,
// dirty-rect clipping, and parallel TBB execution.
// Internal helpers live in dedicated files under executor/:
//   input_resolver.cpp   — resolve_inputs
//   cache_evaluator.cpp  — evaluate_cache
//   node_runner.cpp      — run_node / execute_single_node
//   telemetry_emitter.cpp— emit_node_records
//   tile_pruning.cpp     — compute_dirty_clip
//   framebuffer_lifetime — init / release FB resources

#include <chronon3d/render_graph/graph_executor.hpp>
#include "executor_levels.hpp"
#include "framebuffer_lifetime.hpp"
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <algorithm>
#include <atomic>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace chronon3d::graph {

namespace {
void pin_thread_to_core(int core_id) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif defined(_WIN32)
    SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1) << core_id);
#endif
}
} // namespace

// ──────────────────────────────────────────────────────────────────────
// GraphExecutor public API
// ──────────────────────────────────────────────────────────────────────

GraphExecutor::GraphExecutor()
    : m_arena(std::max(1u, std::thread::hardware_concurrency())) {
    // Pin calling thread (main thread) to core 0
    pin_thread_to_core(0);
}

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    RenderGraph& graph,
    GraphNodeId output,
    RenderGraphContext& ctx,
    FrameArena* arena_override
) {
    // ── Cached execution plan ───────────────────────────────────────
    // Avoid the topological sort + reachability analysis every frame
    // when the graph structure hasn't changed.
    // When ctx.graph_structure_unchanged is true, the caller guarantees
    // the graph topology is identical to the previous call, so we use
    // the cached plan directly without recomputing the structure hash.
    bool plan_cached = false;
    if (ctx.graph_structure_unchanged && m_cached_plan.valid && m_cached_plan.output == output) {
        plan_cached = true;
        if (ctx.counters && ctx.diagnostics_enabled) {
            ctx.counters->execution_plan_cache_hits.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        const uint64_t sig = compute_structure_signature(graph, output);
        if (m_cached_plan.valid &&
            m_cached_plan.structure_hash == sig &&
            m_cached_plan.output == output)
        {
            plan_cached = true;
            if (ctx.counters && ctx.diagnostics_enabled) {
                ctx.counters->execution_plan_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            m_cached_plan.plan = build_execution_plan(graph, output);
            m_cached_plan.structure_hash = sig;
            m_cached_plan.output = output;
            m_cached_plan.valid = true;
        }
    }

    const auto& plan = m_cached_plan.plan;
    if (plan.levels.empty()) {
        return nullptr;
    }

    FrameArena& active_arena = arena_override ? *arena_override : m_frame_arena;
    auto* res = active_arena.resource();
    struct ArenaGuard { 
        FrameArena& arena;
        ~ArenaGuard() { arena.reset(); }
    } guard{active_arena};

    return m_arena.execute([&]() -> std::shared_ptr<Framebuffer> {
        const size_t node_count = graph.size();
        ExecutionState state(res);
        state.temp.resize(node_count);
        state.resolved_key_digest.assign(node_count, 0);
        state.resolved_frame_dependent.assign(node_count, 0);
        state.resolved_cache_hit.assign(node_count, 0);
        state.resolved_bboxes.resize(node_count);

        // ── Pre-allocate shared transparent framebuffer for tile pruning ──
        init_shared_transparent_fb(state, ctx, res);

        auto consumer_remaining = init_consumer_remaining(
            node_count, plan.consumer_counts, res
        );

        auto* parent_counters = profiling::g_current_counters;
        auto* parent_pool = profiling::g_current_framebuffer_pool;

        execute_levels(graph, ctx, state, plan.levels, consumer_remaining, parent_counters, parent_pool, res);

        return state.temp[output];
    });
}

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    FrameArena* arena_override
) {
    auto& graph = compiled.graph;
    const auto& levels = compiled.levels;
    const auto& consumer_counts = compiled.consumer_counts;
    const auto output = compiled.output;

    if (compiled.empty()) {
        return nullptr;
    }

    FrameArena& active_arena = arena_override ? *arena_override : m_frame_arena;
    auto* res = active_arena.resource();
    struct ArenaGuard { 
        FrameArena& arena;
        ~ArenaGuard() { arena.reset(); }
    } guard{active_arena};

    return m_arena.execute([&]() -> std::shared_ptr<Framebuffer> {
        const size_t node_count = graph.size();
        ExecutionState state(res);
        state.temp.resize(node_count);
        state.resolved_key_digest.assign(node_count, 0);
        state.resolved_frame_dependent.assign(node_count, 0);
        state.resolved_cache_hit.assign(node_count, 0);
        state.resolved_bboxes.resize(node_count);

        // ── Pre-allocate shared transparent framebuffer for tile pruning ──
        init_shared_transparent_fb(state, ctx, res);

        auto consumer_remaining = init_consumer_remaining(
            node_count, consumer_counts, res
        );

        auto* parent_counters = profiling::g_current_counters;
        auto* parent_pool = profiling::g_current_framebuffer_pool;

        execute_levels(graph, ctx, state, levels, consumer_remaining, parent_counters, parent_pool, res);

        return state.temp[output];
    });
}

} // namespace chronon3d::graph
