#pragma once

#include <cstddef>
#include <vector>

// Forward declaration — the canonical full definition lives at
// include/chronon3d/core/profiling/render_counter_types.hpp (existing
// public SDK surface).  Forward-declared here at the ROOT `chronon3d`
// namespace (NOT nested inside `chronon3d::graph`) to avoid NAME
// SHADOWING: a forward-decl inside `chronon3d::graph` would create a
// distinct, incomplete `chronon3d::graph::RenderCounters` type, which
// is NOT type-compatible with the canonical `chronon3d::RenderCounters`
// passed at the function-signature boundary (the original pre-P1
// executor_levels.cpp accesses parent_counters->cache_eval_ms against
// the canonical type — a shadowed forward-decl breaks that call site
// with "incomplete type" errors).
//
// Kept as forward decl here to avoid bloating this TU-local header
// (Cat-3 minimal-surface); `level_timings.cpp` includes the full
// definition because `roll_up` actually accesses the `std::atomic<...>`
// counter members via `fetch_add`.
namespace chronon3d {
struct RenderCounters;
}

namespace chronon3d::graph {

// ── LevelTimings: per-level metric accumulator + roll-up counter ──────────
//
// P1 refactor — consolidates the 7 parallel per-node time-tracking
// vectors that were inlined in `executor_levels.cpp` (the
// `level_cache_ms`, `level_dirty_ms`, …, `level_state_ms` locals).
// Each field accumulates one timing category across the level's
// per-node executions and is reported back into `RenderCounters` via
// a single `roll_up()` invocation at the bottom of `execute_levels()`.
//
// Public surface (Cat-3 minimal):
//   - field accessors (the 7 std::vector<double> members — used by
//     `execute_single_node()` to accumulate ms timings)
//   - `resize(n)` — per-level zero-init matching the original
//     `std::vector<double>(level.size(), 0.0)` allocation pattern.
//   - `roll_up(counters, dispatch_ms, input_ms, schedule_ms,
//     framebuffer_ms)` — folds per-node accumulators + 4 whole-level
//     duration deltas + derived overhead_ms into `RenderCounters`
//     via 12 `std::memory_order_relaxed` `fetch_add` operations,
//     byte-equivalent to the pre-P1 executor_levels.cpp roll-up.
//
// The caller is responsible for the `if (parent_counters)` null-check
// and must invoke `roll_up(*parent_counters, ...)` only when
// `parent_counters != nullptr`.  The method takes a non-null
// reference to enforce this.
struct LevelTimings {
    // Per-node accumulators (millisecond doubles, one entry per node in
    // the level).  Indices match the level_index passed to
    // execute_single_node() inside the parallel/sequential branch.
    std::vector<double> cache;
    std::vector<double> dirty;
    std::vector<double> telemetry;
    std::vector<double> execute;
    std::vector<double> predicted_bbox;
    std::vector<double> clone_context;
    std::vector<double> state;

    // Resize all 7 vectors to `n` and zero-initialise.  Implemented
    // via `assign(n, 0.0)` (not `resize(n)`) so the zero-init is
    // unconditional — guards against dirty memory if the LevelTimings
    // instance is ever hoisted or reused outside the per-level scope.
    void resize(std::size_t n);

    // Fold all per-node accumulators + the four whole-level duration
    // deltas into the parent counters and derive overhead_ms.  Parameter
    // type is `::chronon3d::RenderCounters&` (qualified at the root
    // namespace) so the call site gets the canonical type even if some
    // future TU forward-declares `RenderCounters` inside a nested
    // namespace.
    void roll_up(::chronon3d::RenderCounters& counters,
                 double dispatch_ms,
                 double input_ms,
                 double schedule_ms,
                 double framebuffer_ms) const;
};

} // namespace chronon3d::graph
