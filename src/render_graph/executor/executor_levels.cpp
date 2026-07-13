#include "executor_levels.hpp"

#include "framebuffer_lifetime.hpp"
#include "input_resolver.hpp"
#include "level_timings.hpp"
#include "node_runner.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

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
    std::pmr::memory_resource* res,
    const CompiledFrameGraph& compiled
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
            level_resolved[i].resolved_opacity = graph.node(level[i]).evaluate_opacity(ctx.frame_input);
        }
        const auto t_input1 = profiling::now();

        const auto t_schedule1 = profiling::now();

        // P1 — replaced the 7 parallel per-node accumulator vectors with
        // a single `LevelTimings` struct instance.  The struct's `resize(n)`
        // zero-inits all 7 internal vectors; execute_single_node() writes
        // per-node ms values through `&timings.<field>[level_index]` exactly
        // as it did through `&level_*_ms[level_index]`.
        LevelTimings timings;
        timings.resize(level.size());

        // ── PR-B: thread-pool authority === scheduler ────────────────────
        // The previous direct `tbb::parallel_for` call is replaced by
        // `scheduler.for_each_index`: Sequential mode runs serially inside
        // arena(1), TbbFixed(N) parallelises over N slots, TbbAutomatic
        // delegates to TBB's automatic slot allocation.
        // P0 #2 — `level.size() > 0` was tautologically true (the outer
        // for-loop iterates a non-empty `level` by construction); the gate
        // now actually skips the parallel branch for tiny levels on
        // small-slot schedulers, which is the originally-intended semantic.
        // `> 1` (not >= 2): a 1-node level on any scheduler cannot benefit
        // from parallel dispatch — the TBB/task-pool overhead exceeds the
        // single-node work itself.
        const bool use_parallel = level.size() > 1 && scheduler.concurrency() > 1;

        // P0 #2 — Cat-3 minimal-surface: the previous two consecutive
        // `if (parent_counters)` blocks gated both counter groups (parallel
        // vs skipped; parallel vs sequential) with the exact same
        // use_parallel condition; merged into a single branch test so the
        // four fetches inside each arm are grouped + branch-tested once.
        // NOTE: `parallel_regions_skipped_small_level` is now genuinely
        // meaningful (was effectively always 0 under the old tautology);
        // see TICKET-EXECUTOR-CI-COUNTER-REGRESSION-VERIFY.
        if (parent_counters) {
            if (use_parallel) {
                parent_counters->parallel_regions_count.fetch_add(1, std::memory_order_relaxed);
                parent_counters->level_parallel_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                parent_counters->parallel_regions_skipped_small_level.fetch_add(1, std::memory_order_relaxed);
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
                    &timings.cache[level_index],
                    &timings.dirty[level_index],
                    &timings.telemetry[level_index],
                    &timings.execute[level_index],
                    &timings.predicted_bbox[level_index],
                    &timings.clone_context[level_index],
                    &timings.state[level_index],
                    compiled
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
                    &timings.cache[level_index],
                    &timings.dirty[level_index],
                    &timings.telemetry[level_index],
                    &timings.execute[level_index],
                    &timings.predicted_bbox[level_index],
                    &timings.clone_context[level_index],
                    &timings.state[level_index],
                    compiled
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

        // P1 — the 60-line pre-P1 if-block (sum-of-vectors + overhead derive +
        // 12 fetch_add) is absorbed into LevelTimings::roll_up(); the caller
        // is responsible for the null-check on `parent_counters` (executor
        // runs counter-less for non-telemetry callers — same as pre-P1).
        if (parent_counters) {
            timings.roll_up(
                *parent_counters,
                /* dispatch_ms    */ profiling::duration_ms(t_schedule1, t_dispatch1),
                /* input_ms       */ profiling::duration_ms(t_input0, t_input1),
                /* schedule_ms    */ profiling::duration_ms(t_schedule0, t_schedule1),
                /* framebuffer_ms */ profiling::duration_ms(t_fb0, t_fb1)
            );
        }
    }
}

} // namespace chronon3d::graph
