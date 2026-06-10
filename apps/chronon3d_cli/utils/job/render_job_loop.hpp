#pragma once

// ---------------------------------------------------------------------------
// Render Job Frame Loop
//
// Extracts the double-buffered render/write pipeline from
// execute_render_job() into a standalone reusable function.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include "render_job.hpp"

#include <chrono>
#include <vector>

namespace chronon3d::cli {

/// Accumulated state produced by the render-loop phase.
struct RenderLoopResult {
    bool ok{true};
    double total_render_ms{0.0};
    double total_encode_ms{0.0};
    int frames_written{0};

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;

    std::chrono::steady_clock::time_point loop_start;
    std::chrono::steady_clock::time_point loop_end;
};

/// Run the frame render loop (double-buffered or single-frame fallback).
///
/// @param plan     The job plan (read-only).
/// @param renderer The renderer to use for rendering frames.
/// @return         Aggregated result with timing and telemetry.
RenderLoopResult run_render_job_loop(
    const RenderJobPlan& plan,
    SoftwareRenderer& renderer);

} // namespace chronon3d::cli
