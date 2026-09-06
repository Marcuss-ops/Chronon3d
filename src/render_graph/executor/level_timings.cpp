#include "level_timings.hpp"
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace chronon3d::graph {

// OPT-DEAD-TIMINGS — LevelTimings (7 per-node pmr::vector<double>
// accumulators, heap-constructed per level per frame and never written) is
// retired. The whole-level phase-duration fold into RenderCounters is the
// only live consumer and is preserved below as a free function with the
// identical counter update sequence (byte-equivalent fetch_add pattern).
void roll_up_level_durations(RenderCounters& counters,
                             double dispatch_ms,
                             double input_ms,
                             double schedule_ms,
                             double framebuffer_ms) {
    auto add_ms = [](auto& counter, double value) {
        counter.fetch_add(static_cast<uint64_t>(std::llround(value)), std::memory_order_relaxed);
    };
    auto add_us = [](auto& counter, double value) {
        counter.fetch_add(static_cast<uint64_t>(std::llround(value * 1000.0)), std::memory_order_relaxed);
    };

    add_ms(counters.input_resolve_wall_ms, input_ms);
    add_ms(counters.node_schedule_wall_ms, schedule_ms);
    add_ms(counters.node_dispatch_wall_ms, dispatch_ms);
    add_ms(counters.framebuffer_lifetime_wall_ms, framebuffer_ms);
    // Per-node fine-grained categories left the contract with Stage 3; the
    // derived overhead term collapses to the whole dispatch duration.
    add_ms(counters.node_overhead_wall_ms, 0.0);

    add_us(counters.input_resolve_wall_us, input_ms);
    add_us(counters.node_schedule_wall_us, schedule_ms);
    add_us(counters.node_dispatch_wall_us, dispatch_ms);
    add_us(counters.framebuffer_lifetime_wall_us, framebuffer_ms);
    add_us(counters.node_overhead_wall_us, 0.0);
}

} // namespace chronon3d::graph
