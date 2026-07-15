#include "render_job_loop.hpp"
#include "render_job_detail.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>

namespace chronon3d::cli {

RenderLoopResult run_render_job_loop(
    const RenderJob& job,
    SoftwareRenderer& renderer)
{
    RenderLoopResult result;

    if (job.mode == RenderMode::Video) {
        spdlog::error("run_render_job_loop cannot execute Video mode");
        result.ok = false;
        return result;
    }

    const std::int64_t start = job.mode == RenderMode::Still
        ? job.still_frame.integral()
        : job.first_frame.integral();
    const std::int64_t end = job.mode == RenderMode::Still
        ? job.still_frame.integral()
        : job.last_frame.integral();
    const std::int64_t step = std::max<std::int64_t>(
        1, job.frame_step.integral());

    const FrameRange range{start, end, step};
    result.loop_start = profiling::now();

    for (std::int64_t f = start; f <= end; f += step) {
        if (!write_render_frame(*job.comp, renderer, Frame{f}, range, job.output,
                                result.ok, result.telemetry_frames,
                                result.total_render_ms, result.total_encode_ms,
                                result.frames_written)) {
            // Continue to surface all frame failures while preserving false.
        }
    }

    result.loop_end = profiling::now();
    return result;
}

} // namespace chronon3d::cli
