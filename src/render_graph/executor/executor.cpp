// graph_executor.cpp
// Render graph DAG executor with level-scheduling, cache evaluation,
// dirty-rect clipping, and parallel TBB execution.
// Internal helpers (resolve_inputs, evaluate_cache, execute_single_node, etc.)
// live in graph_executor_internal.cpp.

#include <chronon3d/render_graph/graph_executor.hpp>
#include "internal.hpp"
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>
#include <tbb/tbb.h>

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

// ── compute_structure_signature ─────────────────────────────────────

uint64_t GraphExecutor::compute_structure_signature(const RenderGraph& graph, GraphNodeId output) {
    uint64_t sig = hash_value(graph.size());
    for (GraphNodeId id = 0; id < graph.size(); ++id) {
        if (!graph.has_node(id)) continue;
        sig = hash_combine(sig, hash_value(static_cast<int>(graph.node(id).kind())));
        const auto& inputs = graph.inputs(id);
        sig = hash_combine(sig, hash_value(inputs.size()));
        for (GraphNodeId input : inputs) {
            sig = hash_combine(sig, hash_value(input));
        }
    }
    sig = hash_combine(sig, hash_value(output));
    return sig;
}

// ── build_execution_plan (uncached) ─────────────────────────────────

GraphExecutor::ExecutionPlan GraphExecutor::build_execution_plan(RenderGraph& graph, GraphNodeId output) const {
    ExecutionPlan plan;
    const size_t node_count = graph.size();
    if (node_count == 0 || output >= node_count) {
        return plan;
    }

    std::vector<char> reachable(node_count, 0);
    std::vector<GraphNodeId> stack{output};
    while (!stack.empty()) {
        GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id]) {
            continue;
        }
        reachable[id] = 1;
        for (GraphNodeId parent : graph.inputs(id)) {
            stack.push_back(parent);
        }
    }

    std::vector<std::vector<GraphNodeId>> children(node_count);
    std::vector<size_t> indegree(node_count, 0);
    plan.consumer_counts.assign(node_count, 0);

    for (GraphNodeId child = 0; child < node_count; ++child) {
        if (!reachable[child]) {
            continue;
        }
        for (GraphNodeId parent : graph.inputs(child)) {
            if (!reachable[parent]) {
                continue;
            }
            children[parent].push_back(child);
            ++indegree[child];
            ++plan.consumer_counts[parent];
        }
    }

    std::vector<GraphNodeId> current_level;
    current_level.reserve(node_count);
    for (GraphNodeId id = 0; id < node_count; ++id) {
        if (reachable[id] && indegree[id] == 0) {
            current_level.push_back(id);
        }
    }

    size_t scheduled = 0;
    while (!current_level.empty()) {
        plan.levels.push_back(current_level);
        scheduled += current_level.size();

        std::vector<GraphNodeId> next_level;
        for (GraphNodeId id : current_level) {
            for (GraphNodeId child : children[id]) {
                if (--indegree[child] == 0) {
                    next_level.push_back(child);
                }
            }
        }
        current_level.swap(next_level);
    }

    const size_t reachable_count = static_cast<size_t>(
        std::count(reachable.begin(), reachable.end(), static_cast<char>(1))
    );
    if (scheduled != reachable_count) {
        throw std::runtime_error("GraphExecutor: graph is not a DAG or contains unreachable dependency cycles");
    }

    return plan;
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
        // Must happen BEFORE the parallel-for level loop to avoid data races
        // when multiple pruned nodes in the same level try to access it.
        if (ctx.tile_execution_enabled && ctx.active_tile_clip) {
            auto owned_fb = ctx.acquire_owned_fb(ctx.width, ctx.height, false);
            owned_fb->clear(Color::transparent());
            Framebuffer* raw = owned_fb.release();
            PoolFbDeleter deleter{nullptr};
            if (ctx.framebuffer_pool) {
                deleter = PoolFbDeleter{ctx.framebuffer_pool.get(), ctx.framebuffer_pool->alive_token()};
            }
            state.shared_transparent = CachedFB(raw, std::move(deleter));
        }

        std::pmr::vector<std::atomic_size_t> consumer_remaining(node_count, res);
        for (size_t i = 0; i < plan.consumer_counts.size(); ++i) {
            consumer_remaining[i].store(plan.consumer_counts[i], std::memory_order_relaxed);
        }

        auto* parent_counters = profiling::g_current_counters;
        auto* parent_pool = profiling::g_current_framebuffer_pool;

        for (const auto& level : plan.levels) {
            CHRONON_ZONE_C("execute_level", trace_category::kGraph);

            // Sequential pre-resolve phase — extract raw FramebufferRef (no atomics)
            std::pmr::vector<PreResolvedNode> level_resolved(res);
            level_resolved.reserve(level.size());
            for (size_t i = 0; i < level.size(); ++i) {
                level_resolved.emplace_back(res);
                level_resolved[i] = resolve_inputs(graph, level[i], state, consumer_remaining, res);
            }

            // Parallel execution phase — nodes receive FramebufferRef (no shared_ptr copies)
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, level.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t level_index = range.begin(); level_index != range.end(); ++level_index) {
                        execute_single_node(
                            state, graph, ctx, level_resolved, level[level_index], level_index,
                            parent_counters, parent_pool, consumer_remaining
                        );
                    }
                }
            );

            // Decrement consumer counts & release dead references
            for (size_t i = 0; i < level.size(); ++i) {
                const GraphNodeId id = level[i];
                for (GraphNodeId input_id : graph.inputs(id)) {
                    if (!contains_index(consumer_remaining, input_id)) {
                        continue;
                    }
                    if (consumer_remaining[input_id].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        state.temp[input_id].reset();
                        state.resolved_key_digest[input_id] = 0;
                        state.resolved_frame_dependent[input_id] = 0;
                        state.resolved_cache_hit[input_id] = 0;
                        state.resolved_bboxes[input_id].reset();
                    }
                }
            }
        }

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
        // Must happen BEFORE the parallel-for level loop to avoid data races
        // when multiple pruned nodes in the same level try to access it.
        if (ctx.tile_execution_enabled && ctx.active_tile_clip) {
            auto owned_fb = ctx.acquire_owned_fb(ctx.width, ctx.height, false);
            owned_fb->clear(Color::transparent());
            Framebuffer* raw = owned_fb.release();
            PoolFbDeleter deleter{nullptr};
            if (ctx.framebuffer_pool) {
                deleter = PoolFbDeleter{ctx.framebuffer_pool.get(), ctx.framebuffer_pool->alive_token()};
            }
            state.shared_transparent = CachedFB(raw, std::move(deleter));
        }

        std::pmr::vector<std::atomic_size_t> consumer_remaining(node_count, res);
        for (size_t i = 0; i < consumer_counts.size(); ++i) {
            consumer_remaining[i].store(consumer_counts[i], std::memory_order_relaxed);
        }

        auto* parent_counters = profiling::g_current_counters;
        auto* parent_pool = profiling::g_current_framebuffer_pool;

        for (const auto& level : levels) {
            CHRONON_ZONE_C("execute_level", trace_category::kGraph);

            // Sequential pre-resolve phase — extract raw FramebufferRef (no atomics)
            std::pmr::vector<PreResolvedNode> level_resolved(res);
            level_resolved.reserve(level.size());
            for (size_t i = 0; i < level.size(); ++i) {
                level_resolved.emplace_back(res);
                level_resolved[i] = resolve_inputs(graph, level[i], state, consumer_remaining, res);
            }

            // Parallel execution phase — nodes receive FramebufferRef (no shared_ptr copies)
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, level.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t level_index = range.begin(); level_index != range.end(); ++level_index) {
                        execute_single_node(
                            state, graph, ctx, level_resolved, level[level_index], level_index,
                            parent_counters, parent_pool, consumer_remaining
                        );
                    }
                }
            );

            // Decrement consumer counts & release dead references
            for (size_t i = 0; i < level.size(); ++i) {
                const GraphNodeId id = level[i];
                for (GraphNodeId input_id : graph.inputs(id)) {
                    if (!contains_index(consumer_remaining, input_id)) {
                        continue;
                    }
                    if (consumer_remaining[input_id].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        state.temp[input_id].reset();
                        state.resolved_key_digest[input_id] = 0;
                        state.resolved_frame_dependent[input_id] = 0;
                        state.resolved_cache_hit[input_id] = 0;
                        state.resolved_bboxes[input_id].reset();
                    }
                }
            }
        }

        return state.temp[output];
    });
}

} // namespace chronon3d::graph
