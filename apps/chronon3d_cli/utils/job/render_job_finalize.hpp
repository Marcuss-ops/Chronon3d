#pragma once

// ---------------------------------------------------------------------------
// Render Job Finalize Phase
//
// Extracts the telemetry-collection and report-generation sub-steps from the
// monolithic execute_render_job() — wall timing, system counters, telemetry
// record construction, report generation — into a single self-contained
// finaliser.
// ---------------------------------------------------------------------------

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include "render_job.hpp"
#include "render_job_setup.hpp"

#include <vector>

namespace chronon3d::cli {

/// Collect telemetry and generate the execution report after the render loop
/// completes.  Returns true if the overall job is considered successful
/// (even if some frames failed, the telemetry write itself succeeded).
///
/// @param plan         The original job plan (read-only).
/// @param setup        The state produced by setup_render_job().
/// @param telemetry_frames  Per-frame telemetry collected during the loop.
/// @param total_render_ms   Aggregate render time across all frames.
/// @param total_encode_ms   Aggregate encode/write time across all frames.
/// @param frames_written    Number of frames successfully written to disk.
/// @param ok                Whether the render loop completed without fatal
///                          errors (individual frame failures are allowed).
/// @param loop_t0           Clock snapshot taken just before the render loop.
/// @param loop_t1           Clock snapshot taken just after the render loop.
bool finalize_render_job(
    const RenderJobPlan& plan,
    RenderJobSetupResult& setup,
    const std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
    double total_render_ms,
    double total_encode_ms,
    int frames_written,
    bool ok,
    profiling::Clock::time_point loop_t0,
    profiling::Clock::time_point loop_t1);

} // namespace chronon3d::cli
