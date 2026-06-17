#include "frame_timing_recorder.hpp"
#include "frame_state_commit.hpp"
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

void compute_and_record_timings(
    profiling::Clock::time_point t_resolve0,
    profiling::Clock::time_point t_resolve1,
    profiling::Clock::time_point t_dirty0,
    profiling::Clock::time_point t_dirty1,
    profiling::Clock::time_point t_graph0,
    profiling::Clock::time_point t_graph1,
    profiling::Clock::time_point t_exec0,
    profiling::Clock::time_point t_exec1,
    RenderCounters* counters,
    bool diagnostics_enabled,
    Frame frame,
    bool graph_reused)
{
    const double resolve_ms = profiling::duration_ms(t_resolve0, t_resolve1);
    const double dirty_ms = profiling::duration_ms(t_dirty0, t_dirty1);
    const double graph_ms = profiling::duration_ms(t_graph0, t_graph1);
    const double exec_ms = profiling::duration_ms(t_exec0, t_exec1);
    const double total_graph_ms = resolve_ms + dirty_ms + graph_ms + exec_ms;

    if (counters || diagnostics_enabled) {
        record_frame_timings(counters, {
            .resolve_ms       = resolve_ms,
            .dirty_ms         = dirty_ms,
            .graph_ms         = graph_ms,
            .exec_ms          = exec_ms,
            .total_graph_ms   = total_graph_ms,
        });

        if (diagnostics_enabled) {
            spdlog::info(
                "[graph-timing] frame={} resolve_layers_ms={:.2f} dirty_rect_ms={:.2f} "
                "graph_phase_ms={:.2f} graph_exec_ms={:.2f} graph_total_ms={:.2f} graph_reused={}",
                static_cast<int>(frame), resolve_ms, dirty_ms, graph_ms, exec_ms,
                total_graph_ms, graph_reused ? 1 : 0);
        }
    }
}

} // namespace chronon3d::graph
