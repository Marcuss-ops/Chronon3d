#pragma once

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/partitioner.h>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace chronon3d {

namespace detail {

struct alignas(kHardwareDestructiveInterferenceSize) ParallelTrackedWorkerSlot {
    std::uint64_t samples{0};
    std::uint64_t active_sum{0};
    std::uint64_t idle_sum{0};
    std::uint64_t idle_samples{0};
    std::uint64_t peak_local{0};
};

struct ParallelTrackedBinding {
    const void* owner{nullptr};
    std::uint64_t dispatch_id{0};
    std::size_t index{0};
};

inline std::atomic<std::uint64_t> g_parallel_tracked_dispatch_id{1};
inline thread_local ParallelTrackedBinding g_parallel_tracked_binding;

} // namespace detail

/**
 * @brief Tracked parallel_for — wraps tbb::parallel_for and records
 *        active-worker telemetry so that nested parallel regions
 *        (composite blend, transform rows, clear, conversion, etc.)
 *        contribute to tbb_active_workers_peak / avg counters.
 *
 * The worker-tracking mirrors what executor_levels.cpp does at the
 * DAG level.  Without this wrapper, nested tbb::parallel_for calls
 * inside individual nodes are invisible to the telemetry, leading to
 * "tbb active workers peak = 1" even when the node uses all cores.
 *
 * Usage (replace a direct tbb::parallel_for call):
 *   // Before:
 *   tbb::parallel_for(blocked_range<int>(0, n), [&](auto& r) { work(r); });
 *
 *   // After:
 *   parallel_for_tracked(blocked_range<int>(0, n), [&](auto& r) { work(r); });
 *
 * The wrapper picks up profiling::g_current_counters automatically.
 * Pass an explicit counters pointer to override (e.g. ctx.node_exec.counters).
 *
 * When counters is null, the call degrades to plain tbb::parallel_for
 * with zero overhead.
 */
template <typename IndexType, typename Body, typename Partitioner = tbb::auto_partitioner>
void parallel_for_tracked(
    const tbb::blocked_range<IndexType>& range,
    Body&& body,
    RenderCounters* counters = profiling::g_current_counters,
    Partitioner partitioner = Partitioner{})
{
    if (!counters) {
        tbb::parallel_for(range, std::forward<Body>(body), partitioner);
        return;
    }

    // Register each scheduler worker once, then keep all per-task samples in
    // disjoint cache lines/slots.  The old implementation performed five
    // shared RMWs for every callback, making the telemetry itself contend with
    // the pixel work it was measuring.
    const std::size_t max_workers = std::max<std::size_t>(
        1, std::thread::hardware_concurrency());
    struct DispatchTelemetry {
        std::vector<detail::ParallelTrackedWorkerSlot> workers;
        std::atomic<std::size_t> next_worker{0};
        std::uint64_t dispatch_id{0};

        explicit DispatchTelemetry(std::size_t count, std::uint64_t id)
            : workers(count), dispatch_id(id) {}
    } telemetry(
        max_workers,
        detail::g_parallel_tracked_dispatch_id.fetch_add(
            1, std::memory_order_relaxed));

    tbb::parallel_for(
        range,
        [&](const tbb::blocked_range<IndexType>& r) {
            auto& binding = detail::g_parallel_tracked_binding;
            if (binding.owner != &telemetry ||
                binding.dispatch_id != telemetry.dispatch_id) {
                binding.owner = &telemetry;
                binding.dispatch_id = telemetry.dispatch_id;
                binding.index = telemetry.next_worker.fetch_add(
                    1, std::memory_order_relaxed);
                if (binding.index >= telemetry.workers.size()) {
                    binding.index = telemetry.workers.size() - 1;
                }
            }

            auto& local = telemetry.workers[binding.index];
            const std::uint64_t observed_workers = static_cast<std::uint64_t>(
                std::min<std::size_t>(
                    telemetry.next_worker.load(std::memory_order_relaxed),
                    telemetry.workers.size()));
            local.peak_local = std::max(local.peak_local, observed_workers);
            ++local.samples;
            local.active_sum += observed_workers;
            if (observed_workers < max_workers) {
                local.idle_sum += max_workers - observed_workers;
                ++local.idle_samples;
            }

            body(r);
        },
        partitioner
    );

    std::uint64_t peak_workers = 0;
    std::uint64_t active_sum = 0;
    std::uint64_t samples = 0;
    std::uint64_t idle_sum = 0;
    std::uint64_t idle_samples = 0;
    for (const auto& worker : telemetry.workers) {
        active_sum += worker.active_sum;
        samples += worker.samples;
        idle_sum += worker.idle_sum;
        idle_samples += worker.idle_samples;
    }
    for (const auto& worker : telemetry.workers) {
        peak_workers = std::max(peak_workers, worker.peak_local);
    }

    auto recorded_peak = counters->tbb_active_workers_peak.load(
        std::memory_order_relaxed);
    while (recorded_peak < peak_workers &&
           !counters->tbb_active_workers_peak.compare_exchange_weak(
               recorded_peak, peak_workers, std::memory_order_relaxed)) {}
    counters->tbb_active_workers_avg_sum.fetch_add(
        active_sum, std::memory_order_relaxed);
    counters->tbb_active_workers_avg_count.fetch_add(
        samples, std::memory_order_relaxed);
    if (idle_sum > 0) {
        counters->parallel_idle_worker_entry_sum.fetch_add(
            idle_sum, std::memory_order_relaxed);
        counters->parallel_idle_worker_samples.fetch_add(
            idle_samples, std::memory_order_relaxed);
    }
}

} // namespace chronon3d
