#include "executor_levels.hpp"

#include "framebuffer_lifetime.hpp"
#include "input_resolver.hpp"
#include "node_runner.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory_resource>
#include <vector>

namespace chronon3d::graph {

void execute_levels(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    ExecutionState& state,
    ExecutionScheduler& scheduler,
    const std::vector<std::vector<GraphNodeId>>& levels,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    std::pmr::memory_resource* res
) {
    for (const auto& level : levels) {
        CHRONON_ZONE_C("execute_level", trace_category::kGraph);

        const auto t_schedule0 = profiling::now();

        std::pmr::vector<PreResolvedNode> level_resolved(res);
        level_resolved.reserve(level.size());

        // Input resolution is sequential because resolve_inputs allocates
        // from the shared PMR frame arena (bump allocator, not thread-safe).
        const auto t_input0 = profiling::now();
        for (size_t i = 0; i < level.size(); ++i) {
            level_resolved.emplace_back(res);
            level_resolved[i] = resolve_inputs(graph, level[i], state, consumer_remaining, res);
            level_resolved[i].resolved_opacity = graph.node(level[i]).evaluate_opacity(ctx.frame);
        }
        const auto t_input1 = profiling::now();

        const auto t_schedule1 = profiling::now();

        std::vector<double> level_cache_ms(level.size(), 0.0);
        std::vector<double> level_dirty_ms(level.size(), 0.0);
        std::vector<double> level_telemetry_ms(level.size(), 0.0);
        std::vector<double> level_execute_ms(level.size(), 0.0);
        std::vector<double> level_pred_bbox_ms(level.size(), 0.0);
        std::vector<double> level_clone_ctx_ms(level.size(), 0.0);
        std::vector<double> level_state_ms(level.size(), 0.0);

        // ── PR-B: thread-pool authority === scheduler ────────────────────
        // The previous direct `tbb::parallel_for` call is replaced by
        // `scheduler.for_each_index`: Sequential mode runs serially inside
        // arena(1), TbbFixed(N) parallelises over N slots, TbbAutomatic
        // delegates to TBB's automatic slot allocation.
        const bool use_parallel = level.size() > 0;

        if (parent_counters) {
            if (use_parallel) {
                parent_counters->parallel_regions_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                parent_counters->parallel_regions_skipped_small_level.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (parent_counters) {
            if (use_parallel) {
                parent_counters->level_parallel_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                parent_counters->level_sequential_count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (use_parallel) {
            // Tracks how many TBB workers are actively executing nodes
            // at any given moment — used for tbb_active_workers counters.
            // PR-B: peak count is now bounded by scheduler.concurrency(),
            // which can be 1 in Sequential mode.
            std::atomic<int> active_parallel_workers{0};
            std::atomic<uint64_t> idle_worker_us{0};
            std::atomic<int> idle_samples{0};

            const int64_t max_workers =
                static_cast<int64_t>(scheduler.concurrency());

            scheduler.for_each_index(level.size(), [&](std::size_t level_index) {
                // ── Track active worker count ──────────────────────────────
                const int prev = active_parallel_workers.fetch_add(1, std::memory_order_relaxed);
                const int current = prev + 1;

                if (parent_counters) {
                    // Atomic max: update peak if current > stored peak
                    uint64_t peak = parent_counters->tbb_active_workers_peak.load(std::memory_order_relaxed);
                    const uint64_t current_u64 = static_cast<uint64_t>(current);
                    while (peak < current_u64 &&
                           !parent_counters->tbb_active_workers_peak.compare_exchange_weak(
                               peak, current_u64, std::memory_order_relaxed)) {}

                    // Accumulate for average: sum active workers seen at entry
                    parent_counters->tbb_active_workers_avg_sum.fetch_add(
                        current_u64, std::memory_order_relaxed);
                    parent_counters->tbb_active_workers_avg_count.fetch_add(
                        1, std::memory_order_relaxed);

                    // Idle workers = max_workers - current
                    const int idle_now = static_cast<int>(max_workers - current);
                    if (idle_now > 0) {
                        idle_worker_us.fetch_add(
                            static_cast<uint64_t>(idle_now),
                            std::memory_order_relaxed);
                        idle_samples.fetch_add(1, std::memory_order_relaxed);
                    }
                }

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

                // Decrement when this index finishes
                active_parallel_workers.fetch_sub(1, std::memory_order_relaxed);
            });

            // Flush idle worker metrics into parent counters
            if (parent_counters) {
                const uint64_t idle_sum = idle_worker_us.load(std::memory_order_relaxed);
                if (idle_sum > 0) {
                    parent_counters->parallel_idle_worker_entry_sum.fetch_add(
                        idle_sum, std::memory_order_relaxed);
                    parent_counters->parallel_idle_worker_samples.fetch_add(
                        idle_samples.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
                }
            }
        } else {
            const auto t_seq0 = profiling::now();
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
            if (parent_counters) {
                const auto seq_ms = profiling::duration_ms(t_seq0, profiling::now());
                parent_counters->sequential_level_execute_ms.fetch_add(
                    static_cast<uint64_t>(std::llround(seq_ms)),
                    std::memory_order_relaxed);
            }
        }

        const auto t_dispatch1 = profiling::now();

        const auto t_fb0 = profiling::now();
        release_consumed_framebuffers(state, graph, level, consumer_remaining);
        const auto t_fb1 = profiling::now();

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

            const double dispatch_ms = profiling::duration_ms(t_schedule1, t_dispatch1);
            double overhead_ms = dispatch_ms - execute_sum - cache_sum - dirty_sum - telemetry_sum
                                 - pred_bbox_sum - clone_ctx_sum - state_sum;
            if (overhead_ms < 0.0) overhead_ms = 0.0;

            parent_counters->input_resolve_ms.fetch_add(
                static_cast<uint64_t>(std::llround(profiling::duration_ms(t_input0, t_input1))),
                std::memory_order_relaxed);
            parent_counters->node_schedule_ms.fetch_add(
                static_cast<uint64_t>(std::llround(profiling::duration_ms(t_schedule0, t_schedule1))),
                std::memory_order_relaxed);
            parent_counters->node_dispatch_ms.fetch_add(
                static_cast<uint64_t>(std::llround(dispatch_ms)),
                std::memory_order_relaxed);
            parent_counters->framebuffer_lifetime_ms.fetch_add(
                static_cast<uint64_t>(std::llround(profiling::duration_ms(t_fb0, t_fb1))),
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
