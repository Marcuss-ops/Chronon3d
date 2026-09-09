#include "executor_level_dispatch.hpp"

#include "executor_levels.hpp"
#include "node_runner.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
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
        // Per-task RMW telemetry made the measured work contend on the same
        // cache lines as the executor itself. Each scheduler worker registers
        // once per dispatch into a pre-sized slot, then performs plain writes.
        // The caller reduces the slots after the parallel region has joined.
        struct alignas(kHardwareDestructiveInterferenceSize) WorkerTelemetry {
            std::uint64_t samples{0};
            std::uint64_t active_sum{0};
            std::uint64_t idle_sum{0};
            std::uint64_t idle_samples{0};
            std::uint64_t nodes_executed{0};
            std::uint64_t peak_local{0};
        };
        struct DispatchTelemetry {
            std::vector<WorkerTelemetry> workers;
            std::atomic<std::size_t> next_worker{0};
            std::uint64_t dispatch_id{0};
            explicit DispatchTelemetry(std::size_t count, std::uint64_t id)
                : workers(count), dispatch_id(id) {}
        };
        static std::atomic<std::uint64_t> next_dispatch_id{1};
        DispatchTelemetry telemetry(
            static_cast<std::size_t>(std::max(1, scheduler.concurrency())),
            next_dispatch_id.fetch_add(1, std::memory_order_relaxed));
        struct WorkerBinding {
            const void* owner{nullptr};
            std::uint64_t dispatch_id{0};
            std::size_t index{0};
        };
        thread_local WorkerBinding binding;
        const std::uint64_t max_workers = static_cast<std::uint64_t>(
            std::max(1, scheduler.concurrency()));

        scheduler.for_each_index(level.size(), [&](std::size_t level_index) {
            if (binding.owner != &telemetry ||
                binding.dispatch_id != telemetry.dispatch_id) {
                binding.owner = &telemetry;
                binding.dispatch_id = telemetry.dispatch_id;
                binding.index = telemetry.next_worker.fetch_add(
                    1, std::memory_order_relaxed);
                // TBB cannot execute more concurrent callbacks than the
                // arena's concurrency. This guard keeps malformed scheduler
                // implementations fail-safe rather than indexing past the
                // fixed worker-local array.
                if (binding.index >= telemetry.workers.size()) {
                    binding.index = telemetry.workers.size() - 1;
                }
            }
            auto& local = telemetry.workers[binding.index];
            const std::uint64_t observed_workers = static_cast<std::uint64_t>(
                std::min<std::size_t>(telemetry.next_worker.load(std::memory_order_relaxed),
                                      telemetry.workers.size()));
            local.peak_local = std::max(local.peak_local, observed_workers);
            ++local.samples;
            local.active_sum += observed_workers;
            if (observed_workers < max_workers) {
                local.idle_sum += max_workers - observed_workers;
                ++local.idle_samples;
            }

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
            for (const auto& worker : telemetry.workers) {
                active_sum += worker.active_sum;
                samples += worker.samples;
                idle_sum += worker.idle_sum;
                idle_samples += worker.idle_samples;
                nodes_executed += worker.nodes_executed;
            }
            for (const auto& worker : telemetry.workers) {
                peak_workers = std::max(peak_workers, worker.peak_local);
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
