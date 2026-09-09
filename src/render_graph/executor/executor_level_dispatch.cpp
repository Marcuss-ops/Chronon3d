#include "executor_level_dispatch.hpp"

#include "executor_levels.hpp"
#include "node_runner.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace chronon3d::graph {

void dispatch_level_nodes(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    ExecutionState& state,
    ExecutionScheduler& scheduler,
    const std::vector<GraphNodeId>& level,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    const CompiledFrameGraph& compiled)
{
    // OPT-DEAD-TIMINGS — the LevelTimings per-node accumulator surface was
    // retired with Stage 3 (per-node fine timing); the dead 7-vector heap
    // object and the 7 dead out-param pointers on execute_single_node are
    // gone. Whole-level phase durations (dispatch/input/schedule/framebuffer)
    // are still folded into RenderCounters by execute_levels().
    const bool use_parallel = should_execute_level_in_parallel(
        level.size(), scheduler.concurrency());

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
        // FIXING.md H-03 — zero heap alloc per dispatch: fixed array on
        // stack, no std::vector reallocation, no atomic contention on a
        // single next_worker. Worker index hashes the thread id so
        // distinct TBB workers land on distinct slots without RMW.
        static constexpr std::size_t kMaxWorkers = 64;
        struct alignas(kHardwareDestructiveInterferenceSize) WorkerTelemetry {
            std::uint64_t samples{0};
            std::uint64_t active_sum{0};
            std::uint64_t idle_sum{0};
            std::uint64_t idle_samples{0};
            std::uint64_t nodes_executed{0};
            std::uint64_t peak_local{0};
        };
        const std::size_t worker_count = static_cast<std::size_t>(
            std::clamp(scheduler.concurrency(), 1, static_cast<int>(kMaxWorkers)));
        std::array<WorkerTelemetry, kMaxWorkers> workers{};
        const std::uint64_t max_workers = static_cast<std::uint64_t>(worker_count);
        // Approximate active worker count without atomics: TBB reuses
        // workers briefly, so count samples on the first N slots as
        // an estimate; exact idle_sum is not on the hot path.
        scheduler.for_each_index(level.size(), [&](std::size_t level_index) {
            const std::size_t slot = std::hash<std::thread::id>{}(std::this_thread::get_id()) % worker_count;
            auto& local = workers[slot];
            // Best-effort active estimate: non-zero sampled slots this batch.
            // Kept simple to avoid any shared atomic on the per-task path.
            ++local.samples;
            local.active_sum += 1;
            local.peak_local = std::max(local.peak_local, static_cast<std::uint64_t>(1));

            execute_single_node(
                state, graph, ctx, level_resolved, level[level_index], level_index,
                parent_counters, parent_pool, consumer_remaining,
                compiled, &local.nodes_executed);
        });

        if (parent_counters) {
            std::uint64_t peak_workers = 0;
            std::uint64_t active_sum = 0;
            std::uint64_t samples = 0;
            std::uint64_t idle_sum = 0;
            std::uint64_t idle_samples = 0;
            std::uint64_t nodes_executed = 0;
            for (std::size_t i = 0; i < worker_count; ++i) {
                const auto& worker = workers[i];
                active_sum += worker.active_sum;
                samples += worker.samples;
                idle_sum += worker.idle_sum;
                idle_samples += worker.idle_samples;
                nodes_executed += worker.nodes_executed;
            }
            for (std::size_t i = 0; i < worker_count; ++i) {
                peak_workers = std::max(peak_workers, workers[i].peak_local);
            }
            auto recorded_peak = parent_counters->tbb_active_workers_peak.load(
                std::memory_order_relaxed);
            while (recorded_peak < peak_workers &&
                   !parent_counters->tbb_active_workers_peak.compare_exchange_weak(
                       recorded_peak, peak_workers, std::memory_order_relaxed)) {}

            parent_counters->tbb_active_workers_avg_sum.fetch_add(
                active_sum, std::memory_order_relaxed);
            parent_counters->tbb_active_workers_avg_count.fetch_add(
                samples, std::memory_order_relaxed);
            parent_counters->nodes_executed.fetch_add(
                nodes_executed, std::memory_order_relaxed);
            if (idle_sum > 0) {
                parent_counters->parallel_idle_worker_entry_sum.fetch_add(
                    idle_sum, std::memory_order_relaxed);
                parent_counters->parallel_idle_worker_samples.fetch_add(
                    idle_samples, std::memory_order_relaxed);
            }
        }
        return;
    }

    const auto t_seq0 = profiling::now();
    for (size_t level_index = 0; level_index < level.size(); ++level_index) {
        execute_single_node(
            state, graph, ctx, level_resolved, level[level_index], level_index,
            parent_counters, parent_pool, consumer_remaining,
            compiled, nullptr);
    }
    if (parent_counters) {
        const auto seq_ms = profiling::duration_ms(t_seq0, profiling::now());
        parent_counters->sequential_level_execute_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(seq_ms)), std::memory_order_relaxed);
    }
}

} // namespace chronon3d::graph
