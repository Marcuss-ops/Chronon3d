#pragma once

// ---------------------------------------------------------------------------
// frame_timing_recorder.hpp
//
/// @file    frame_timing_recorder.hpp
/// @brief   Phase timing computation and telemetry recording.
///          Extracted from scene.cpp so the orchestrator doesn't need to
///          compute deltas and log diagnostics inline.
//
// Uses profiling::Clock::time_point (std::chrono::steady_clock::time_point)
// and the FrameTimings struct from frame_state_commit.hpp.
// ===========================================================================

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>

namespace chronon3d::graph {

/// Compute phase timings from raw time points and record to telemetry counters.
/// Logs diagnostics timing info when enabled.
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
    bool graph_reused);

} // namespace chronon3d::graph
