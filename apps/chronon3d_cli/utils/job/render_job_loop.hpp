#pragma once

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include "render_job.hpp"

#include <chrono>
#include <vector>

namespace chronon3d::cli {

struct RenderLoopResult {
    bool ok{true};
    double total_render_ms{0.0};
    double total_encode_ms{0.0};
    int frames_written{0};

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;

    std::chrono::steady_clock::time_point loop_start;
    std::chrono::steady_clock::time_point loop_end;
};

RenderLoopResult run_render_job_loop(
    const RenderJob& job,
    SoftwareRenderer& renderer);

} // namespace chronon3d::cli
