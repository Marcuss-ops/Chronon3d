#include "executor_level_dispatch.hpp"

#include "executor_levels.hpp"
#include "node_runner.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <spdlog/spdlog.h>

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
        // stack, no std::vector reallocation.  A thread-id hash is only a
        // distribution hint, not an ownership proof: distinct workers may
        // collide modulo worker_count.  The fields are therefore atomic.
        static constexpr std::size_t kMaxWorkers = 64;
        struct alignas(kHardwareDestructiveInterferenceSize) WorkerTelemetry {
            std::atomic<std::uint64_t> samples{0};
            std::atomic<std::uint64_t> active_sum{0};
            std::atomic<std::uint64_t> peak_local{0};
        };
        const std::size_t worker_count = static_cast<std::size_t>(
            std::clamp(scheduler.concurrency(), 1, static_cast<int>(kMaxWorkers)));
        std::array<WorkerTelemetry, kMaxWorkers> workers{};
        scheduler.for_each_index(level.size(), [&](std::size_t level_index) {
            const std::size_t slot = std::hash<std::thread::id>{}(std::this_thread::get_id()) % worker_count;
            auto& local = workers[slot];
            local.samples.fetch_add(1, std::memory_order_relaxed);
            local.active_sum.fetch_add(1, std::memory_order_relaxed);
            auto peak = local.peak_local.load(std::memory_order_relaxed);
            while (peak < 1 && !local.peak_local.compare_exchange_weak(
                       peak, 1, std::memory_order_relaxed)) {}

            const auto node_t0 = profiling::now();
            const auto node_id = level[level_index];
            execute_single_node(
                state, graph, ctx, level_resolved, node_id, level_index,
                parent_counters, parent_pool, consumer_remaining,
                compiled, nullptr);
            if (ctx.frame_input.frame <= 2) {
                const double node_ms = profiling::duration_ms(node_t0, profiling::now());
                spdlog::info("[exec-node-diag] frame={} (parallel) node_id={} name='{}' ms={:.3f}",
                             static_cast<int>(ctx.frame_input.frame), node_id,
                             graph.node(node_id).name(), node_ms);
            }
        });

        if (parent_counters) {
            std::uint64_t peak_workers = 0;
            std::uint64_t active_sum = 0;
            std::uint64_t samples = 0;
            for (std::size_t i = 0; i < worker_count; ++i) {
                const auto& worker = workers[i];
                active_sum += worker.active_sum.load(std::memory_order_relaxed);
                samples += worker.samples.load(std::memory_order_relaxed);
            }
            for (std::size_t i = 0; i < worker_count; ++i) {
                peak_workers = std::max(
                    peak_workers, workers[i].peak_local.load(std::memory_order_relaxed));
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
        }
        return;
    }

    const auto t_seq0 = profiling::now();
    for (size_t level_index = 0; level_index < level.size(); ++level_index) {
        const auto node_t0 = profiling::now();
        const auto node_id = level[level_index];
        execute_single_node(
            state, graph, ctx, level_resolved, node_id, level_index,
            parent_counters, parent_pool, consumer_remaining,
            compiled, nullptr);
        if (ctx.frame_input.frame <= 2) {
            const double node_ms = profiling::duration_ms(node_t0, profiling::now());
            spdlog::info("[exec-node-diag] frame={} (seq) node_id={} name='{}' ms={:.3f}",
                         static_cast<int>(ctx.frame_input.frame), node_id,
                         graph.node(node_id).name(), node_ms);
        }
    }
    if (parent_counters) {
        const auto seq_ms = profiling::duration_ms(t_seq0, profiling::now());
        parent_counters->sequential_level_execute_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(seq_ms)), std::memory_order_relaxed);
    }
}

} // namespace chronon3d::graph
