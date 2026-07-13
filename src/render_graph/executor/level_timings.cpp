#include "level_timings.hpp"

// Canonical SDK header for the full RenderCounters definition (root
// namespace `chronon3d::RenderCounters`).  The forward-decl in
// `level_timings.hpp` is intentionally at the root namespace (NOT inside
// `chronon3d::graph`) to prevent the shadow-type rot; this include
// pulls the full definition so `roll_up` can access the `std::atomic<...>`
// counter members via `fetch_add` without compile errors.
#include <chronon3d/core/profiling/render_counter_types.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>

namespace chronon3d::graph {

// P1 — resize() implementation: zero-init all 7 per-node accumulators
// to `n` entries each via `assign(n, 0.0)`.  `assign` is used instead
// of `resize` to make the zero-init unconditional — even on hoisted /
// reused instances, the pre-existing values are dropped, matching
// the original `std::vector<double>(level.size(), 0.0)` allocation
// pattern from executor_levels.cpp.
void LevelTimings::resize(std::size_t n) {
    cache.assign(n, 0.0);
    dirty.assign(n, 0.0);
    telemetry.assign(n, 0.0);
    execute.assign(n, 0.0);
    predicted_bbox.assign(n, 0.0);
    clone_context.assign(n, 0.0);
    state.assign(n, 0.0);
}

// P1 — roll_up() implementation: byte-equivalent to the pre-P1
// executor_levels.cpp:185-243 if-block.  Performs:
//   1. O(7n) sum of the 7 per-node accumulators across the level.
//   2. overhead_ms = dispatch_ms - sum(7 per-node fields), clamped to
//      >= 0 because the per-node sums can occasionally exceed
//      dispatch_ms due to clock-resolution noise on tiny levels.
//   3. 12 fetch_add operations onto the parent RenderCounters
//      (4 whole-level duration deltas + 7 per-field sums + 1
//      derived overhead_ms).  Memory order is `std::memory_order_relaxed`
//      throughout — counters are statistical, no synchronisation role.
void LevelTimings::roll_up(RenderCounters& counters,
                           double dispatch_ms,
                           double input_ms,
                           double schedule_ms,
                           double framebuffer_ms) const {
    // Step 1: sum-of-vector across the 7 timing fields.  Total
    // iteration count is taken from `execute.size()` because all 7
    // vectors share the same size (set by `resize(n)`); using a
    // canonical vector avoids subtle inconsistencies if the caller
    // ever forgot to resize one of the seven.
    double cache_sum = 0.0;
    double dirty_sum = 0.0;
    double telemetry_sum = 0.0;
    double execute_sum = 0.0;
    double pred_bbox_sum = 0.0;
    double clone_ctx_sum = 0.0;
    double state_sum = 0.0;

    const std::size_t n = execute.size();
    for (std::size_t i = 0; i < n; ++i) {
        cache_sum     += cache[i];
        dirty_sum     += dirty[i];
        telemetry_sum += telemetry[i];
        execute_sum   += execute[i];
        pred_bbox_sum += predicted_bbox[i];
        clone_ctx_sum += clone_context[i];
        state_sum     += state[i];
    }

    // Step 2: derived overhead_ms.  Computed inline here (rather than
    // at the call site) so the overhead's mathematical relationship
    // to dispatch_ms and the per-node sums is encapsulated with the
    // timing struct, not duplicated at every executor call site.
    double overhead_ms = dispatch_ms
                       - execute_sum - cache_sum - dirty_sum - telemetry_sum
                       - pred_bbox_sum - clone_ctx_sum - state_sum;
    if (overhead_ms < 0.0) overhead_ms = 0.0;

    // Step 3: 12 fetch_add operations onto parent counters.  Order
    // matches pre-P1 executor_levels.cpp:200-238 for byte-equivalence
    // on counters (the cumulative order matters because some counters
    // are diagnostic and may be post-processed in the order they
    // were emitted).  All fetch_add calls use `std::memory_order_relaxed`
    // inline (NOT a `using` alias — `memory_order_relaxed` is an enum
    // value of type `std::memory_order`, not a type itself, so a
    // `using` alias would fail to compile with "'memory_order_relaxed'
    // does not name a type").

    counters.input_resolve_ms.fetch_add(
        static_cast<uint64_t>(std::llround(input_ms)), std::memory_order_relaxed);
    counters.node_schedule_ms.fetch_add(
        static_cast<uint64_t>(std::llround(schedule_ms)), std::memory_order_relaxed);
    counters.node_dispatch_ms.fetch_add(
        static_cast<uint64_t>(std::llround(dispatch_ms)), std::memory_order_relaxed);
    counters.framebuffer_lifetime_ms.fetch_add(
        static_cast<uint64_t>(std::llround(framebuffer_ms)), std::memory_order_relaxed);

    counters.cache_eval_ms.fetch_add(
        static_cast<uint64_t>(std::llround(cache_sum)), std::memory_order_relaxed);
    counters.dirty_eval_ms.fetch_add(
        static_cast<uint64_t>(std::llround(dirty_sum)), std::memory_order_relaxed);
    counters.telemetry_emit_ms.fetch_add(
        static_cast<uint64_t>(std::llround(telemetry_sum)), std::memory_order_relaxed);
    counters.node_execute_actual_ms.fetch_add(
        static_cast<uint64_t>(std::llround(execute_sum)), std::memory_order_relaxed);
    counters.predicted_bbox_ms.fetch_add(
        static_cast<uint64_t>(std::llround(pred_bbox_sum)), std::memory_order_relaxed);
    counters.clone_context_ms.fetch_add(
        static_cast<uint64_t>(std::llround(clone_ctx_sum)), std::memory_order_relaxed);
    counters.state_assign_ms.fetch_add(
        static_cast<uint64_t>(std::llround(state_sum)), std::memory_order_relaxed);

    counters.node_overhead_ms.fetch_add(
        static_cast<uint64_t>(std::llround(overhead_ms)), std::memory_order_relaxed);
}

} // namespace chronon3d::graph
