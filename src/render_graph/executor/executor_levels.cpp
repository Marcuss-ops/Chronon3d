#include "executor_levels.hpp"

#include "framebuffer_lifetime.hpp"
#include "input_resolver.hpp"
#include "node_runner.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

#include <chrono>
#include <cmath>
#include <tbb/tbb.h>

namespace chronon3d::graph {

void execute_levels(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    ExecutionState& state,
    const std::vector<std::vector<GraphNodeId>>& levels,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    std::pmr::memory_resource* res
) {
    for (const auto& level : levels) {
        CHRONON_ZONE_C("execute_level", trace_category::kGraph);

        const auto t_schedule0 = std::chrono::steady_clock::now();

        std::pmr::vector<PreResolvedNode> level_resolved(res);
        level_resolved.reserve(level.size());

        // Input resolution is sequential because resolve_inputs allocates
        // from the shared PMR frame arena (bump allocator, not thread-safe).
        const auto t_input0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < level.size(); ++i) {
            level_resolved.emplace_back(res);
            level_resolved[i] = resolve_inputs(graph, level[i], state, consumer_remaining, res);
        }
        const auto t_input1 = std::chrono::steady_clock::now();

        const auto t_schedule1 = std::chrono::steady_clock::now();

        std::vector<double> level_cache_ms(level.size(), 0.0);
        std::vector<double> level_dirty_ms(level.size(), 0.0);
        std::vector<double> level_telemetry_ms(level.size(), 0.0);
        std::vector<double> level_execute_ms(level.size(), 0.0);
        std::vector<double> level_pred_bbox_ms(level.size(), 0.0);
        std::vector<double> level_clone_ctx_ms(level.size(), 0.0);
        std::vector<double> level_state_ms(level.size(), 0.0);

        // Parallelize node execution for levels with multiple independent nodes.
        // Nodes within the same level have no data dependencies — they only read
        // from previously-completed levels (state.temp, state.resolved_*), so
        // parallel execution is safe.  Each node writes to its own state.temp[id]
        // slot, and shared data (counters, cache, pool) uses atomics/mutexes.
        const bool use_parallel = level.size() > 1;

        if (parent_counters) {
            if (use_parallel) {
                parent_counters->level_parallel_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                parent_counters->level_sequential_count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (use_parallel) {
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, level.size()),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t level_index = range.begin(); level_index != range.end(); ++level_index) {
                        execute_single_node(
                            state,
                            graph,
                            ctx,
                            level_resolved,
                            level[level_index],
                            level_index,
                            parent_counters,
                            parent_pool,
                            consumer_remaining,
                            &level_cache_ms[level_index],
                            &level_dirty_ms[level_index],
                            &level_telemetry_ms[level_index],
                            &level_execute_ms[level_index],
                            &level_pred_bbox_ms[level_index],
                            &level_clone_ctx_ms[level_index],
                            &level_state_ms[level_index]
                        );
                    }
                }
            );
        } else {
            for (size_t level_index = 0; level_index < level.size(); ++level_index) {
                execute_single_node(
                    state,
                    graph,
                    ctx,
                    level_resolved,
                    level[level_index],
                    level_index,
                    parent_counters,
                    parent_pool,
                    consumer_remaining,
                    &level_cache_ms[level_index],
                    &level_dirty_ms[level_index],
                    &level_telemetry_ms[level_index],
                    &level_execute_ms[level_index],
                    &level_pred_bbox_ms[level_index],
                    &level_clone_ctx_ms[level_index],
                    &level_state_ms[level_index]
                );
            }
        }

        const auto t_dispatch1 = std::chrono::steady_clock::now();

        const auto t_fb0 = std::chrono::steady_clock::now();
        release_consumed_framebuffers(state, graph, level, consumer_remaining);
        const auto t_fb1 = std::chrono::steady_clock::now();

        if (parent_counters) {
            double cache_sum = 0.0, dirty_sum = 0.0, telemetry_sum = 0.0, execute_sum = 0.0;
            double pred_bbox_sum = 0.0, clone_ctx_sum = 0.0, state_sum = 0.0;
            for (size_t i = 0; i < level.size(); ++i) {
                cache_sum += level_cache_ms[i];
                dirty_sum += level_dirty_ms[i];
                telemetry_sum += level_telemetry_ms[i];
                execute_sum += level_execute_ms[i];
                pred_bbox_sum += level_pred_bbox_ms[i];
                clone_ctx_sum += level_clone_ctx_ms[i];
                state_sum += level_state_ms[i];
            }

            const double dispatch_ms = std::chrono::duration<double, std::milli>(t_dispatch1 - t_schedule1).count();
            double overhead_ms = dispatch_ms - execute_sum - cache_sum - dirty_sum - telemetry_sum
                                 - pred_bbox_sum - clone_ctx_sum - state_sum;
            if (overhead_ms < 0.0) overhead_ms = 0.0;

            parent_counters->input_resolve_ms.fetch_add(
                static_cast<uint64_t>(std::llround(std::chrono::duration<double, std::milli>(t_input1 - t_input0).count())),
                std::memory_order_relaxed);
            parent_counters->node_schedule_ms.fetch_add(
                static_cast<uint64_t>(std::llround(std::chrono::duration<double, std::milli>(t_schedule1 - t_schedule0).count())),
                std::memory_order_relaxed);
            parent_counters->node_dispatch_ms.fetch_add(
                static_cast<uint64_t>(std::llround(dispatch_ms)),
                std::memory_order_relaxed);
            parent_counters->framebuffer_lifetime_ms.fetch_add(
                static_cast<uint64_t>(std::llround(std::chrono::duration<double, std::milli>(t_fb1 - t_fb0).count())),
                std::memory_order_relaxed);
            parent_counters->cache_eval_ms.fetch_add(
                static_cast<uint64_t>(std::llround(cache_sum)),
                std::memory_order_relaxed);
            parent_counters->dirty_eval_ms.fetch_add(
                static_cast<uint64_t>(std::llround(dirty_sum)),
                std::memory_order_relaxed);
            parent_counters->telemetry_emit_ms.fetch_add(
                static_cast<uint64_t>(std::llround(telemetry_sum)),
                std::memory_order_relaxed);
            parent_counters->node_execute_actual_ms.fetch_add(
                static_cast<uint64_t>(std::llround(execute_sum)),
                std::memory_order_relaxed);
            parent_counters->predicted_bbox_ms.fetch_add(
                static_cast<uint64_t>(std::llround(pred_bbox_sum)),
                std::memory_order_relaxed);
            parent_counters->clone_context_ms.fetch_add(
                static_cast<uint64_t>(std::llround(clone_ctx_sum)),
                std::memory_order_relaxed);
            parent_counters->state_assign_ms.fetch_add(
                static_cast<uint64_t>(std::llround(state_sum)),
                std::memory_order_relaxed);
            parent_counters->node_overhead_ms.fetch_add(
                static_cast<uint64_t>(std::llround(overhead_ms)),
                std::memory_order_relaxed);
        }
    }
}

} // namespace chronon3d::graph
