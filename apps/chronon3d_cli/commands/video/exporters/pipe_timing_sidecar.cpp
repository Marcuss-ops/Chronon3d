#include "pipe_timing_sidecar.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace chronon3d::cli {
using pipe_timing::JobTimings;

void write_frame_timing_sidecar(
    const std::string& video_path,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& render_frames,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& encoder_frames,
    double wall_time_ms,
    double render_ms,
    double encode_ms,
    const JobTimings& timings,
    bool is_native)
{
#include "pipe_timing_sidecar_frames.inc"
#include "pipe_timing_sidecar_job.inc"
#include "pipe_timing_sidecar_output.inc"
}

} // namespace chronon3d::cli
