#include "level_timings.hpp"
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace chronon3d::graph {

// Stage 3: detailed per-node timing is no longer allocated on the production
// path. The seven vectors remain as a compatibility surface for diagnostic
// callers, but normal level dispatch passes nullptr and keeps them empty.
void LevelTimings::resize(std::size_t) {
    cache.clear();
    dirty.clear();
    telemetry.clear();
    execute.clear();
    predicted_bbox.clear();
    clone_context.clear();
    state.clear();
}

void LevelTimings::roll_up(RenderCounters& counters,
                           double dispatch_ms,
                           double input_ms,
                           double schedule_ms,
                           double framebuffer_ms) const {
    double cache_sum = 0.0, dirty_sum = 0.0, telemetry_sum = 0.0,
           execute_sum = 0.0, pred_bbox_sum = 0.0,
           clone_ctx_sum = 0.0, state_sum = 0.0;
    const std::size_t n = execute.size();
    for (std::size_t i = 0; i < n; ++i) {
        cache_sum += cache[i];
        dirty_sum += dirty[i];
        telemetry_sum += telemetry[i];
        execute_sum += execute[i];
        pred_bbox_sum += predicted_bbox[i];
        clone_ctx_sum += clone_context[i];
        state_sum += state[i];
    }

    double overhead_ms = dispatch_ms - execute_sum - cache_sum - dirty_sum - telemetry_sum
                       - pred_bbox_sum - clone_ctx_sum - state_sum;
    if (overhead_ms < 0.0) overhead_ms = 0.0;

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
    add_ms(counters.cache_eval_wall_ms, cache_sum);
    add_ms(counters.dirty_eval_wall_ms, dirty_sum);
    add_ms(counters.telemetry_emit_wall_ms, telemetry_sum);
    add_ms(counters.node_execute_actual_wall_ms, execute_sum);
    add_ms(counters.predicted_bbox_wall_ms, pred_bbox_sum);
    add_ms(counters.clone_context_wall_ms, clone_ctx_sum);
    add_ms(counters.state_assign_wall_ms, state_sum);
    add_ms(counters.node_overhead_wall_ms, overhead_ms);

    add_us(counters.input_resolve_wall_us, input_ms);
    add_us(counters.node_schedule_wall_us, schedule_ms);
    add_us(counters.node_dispatch_wall_us, dispatch_ms);
    add_us(counters.framebuffer_lifetime_wall_us, framebuffer_ms);
    add_us(counters.cache_eval_wall_us, cache_sum);
    add_us(counters.dirty_eval_wall_us, dirty_sum);
    add_us(counters.telemetry_emit_wall_us, telemetry_sum);
    add_us(counters.node_execute_actual_wall_us, execute_sum);
    add_us(counters.predicted_bbox_wall_us, pred_bbox_sum);
    add_us(counters.clone_context_wall_us, clone_ctx_sum);
    add_us(counters.state_assign_wall_us, state_sum);
    add_us(counters.node_overhead_wall_us, overhead_ms);
}

} // namespace chronon3d::graph
