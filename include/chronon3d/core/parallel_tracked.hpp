#pragma once

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace chronon3d {

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
 * Pass an explicit counters pointer to override (e.g. ctx.counters).
 *
 * When counters is null, the call degrades to plain tbb::parallel_for
 * with zero overhead.
 */
template <typename IndexType, typename Body>
void parallel_for_tracked(
    const tbb::blocked_range<IndexType>& range,
    Body&& body,
    RenderCounters* counters = profiling::g_current_counters)
{
    if (!counters) {
        tbb::parallel_for(range, std::forward<Body>(body));
        return;
    }

    // Shared atomic: each worker increments on entry, decrements on exit.
    // This measures the true concurrency WITHIN this single parallel_for.
    std::atomic<int> active_workers{0};
    const int64_t max_workers = static_cast<int64_t>(
        std::thread::hardware_concurrency());

    tbb::parallel_for(
        range,
        [&](const tbb::blocked_range<IndexType>& r) {
            const int prev = active_workers.fetch_add(1,
                std::memory_order_relaxed);
            const int current = prev + 1;
            const uint64_t current_u64 = static_cast<uint64_t>(current);

            // ── Peak: atomic max ────────────────────────────────────
            uint64_t peak = counters->tbb_active_workers_peak.load(
                std::memory_order_relaxed);
            while (peak < current_u64 &&
                   !counters->tbb_active_workers_peak.compare_exchange_weak(
                       peak, current_u64, std::memory_order_relaxed)) {}

            // ── Average accumulation ────────────────────────────────
            counters->tbb_active_workers_avg_sum.fetch_add(
                current_u64, std::memory_order_relaxed);
            counters->tbb_active_workers_avg_count.fetch_add(
                1, std::memory_order_relaxed);

            // ── Idle-worker tracking ────────────────────────────────
            const int idle_now = static_cast<int>(max_workers - current);
            if (idle_now > 0) {
                counters->parallel_idle_worker_entry_sum.fetch_add(
                    static_cast<uint64_t>(idle_now),
                    std::memory_order_relaxed);
                counters->parallel_idle_worker_samples.fetch_add(
                    1, std::memory_order_relaxed);
            }

            // ── Execute the wrapped body ────────────────────────────
            body(r);

            active_workers.fetch_sub(1, std::memory_order_relaxed);
        }
    );
}

} // namespace chronon3d
