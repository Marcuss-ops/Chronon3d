#pragma once

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include "render_job.hpp"
#include "render_job_setup.hpp"

#include <vector>

namespace chronon3d::cli {

bool finalize_render_job(
    const RenderJob& job,
    RenderJobSetupResult& setup,
    const std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
    double total_render_ms,
    double total_encode_ms,
    int frames_written,
    bool ok,
    profiling::Clock::time_point loop_t0,
    profiling::Clock::time_point loop_t1);

} // namespace chronon3d::cli
