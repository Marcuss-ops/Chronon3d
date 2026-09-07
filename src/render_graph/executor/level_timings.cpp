#include "level_timings.hpp"
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace chronon3d::graph {

// OPT-DEAD-TIMINGS — whole-level phase fold.
//
// The per-node LevelTimings accumulator surface was removed (Stage 3): the
// executor no longer allocates seven pmr::vectors per level per frame.
// execute_levels() now folds the four measured whole-level phase durations
// into RenderCounters with the identical relaxed fetch_add pattern the
// struct's roll_up() used for those phases. No overhead residual exists in
// this mode: node_dispatch_wall_ms already receives the dispatch duration,
// so reporting the same wall time again as overhead would double-count.
void roll_up_level_durations(::chronon3d::RenderCounters& counters,
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

    add_us(counters.input_resolve_wall_us, input_ms);
    add_us(counters.node_schedule_wall_us, schedule_ms);
    add_us(counters.node_dispatch_wall_us, dispatch_ms);
    add_us(counters.framebuffer_lifetime_wall_us, framebuffer_ms);
}

} // namespace chronon3d::graph
