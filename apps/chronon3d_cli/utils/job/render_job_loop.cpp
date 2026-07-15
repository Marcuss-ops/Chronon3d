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

    result.loop_start = profiling::now();

    if (!job.selected_frames.empty()) {
        auto [min_it, max_it] = std::minmax_element(
            job.selected_frames.begin(), job.selected_frames.end(),
            [](Frame lhs, Frame rhs) {
                return lhs.integral() < rhs.integral();
            });

        const std::int64_t range_start = min_it->integral();
        std::int64_t range_end = max_it->integral();
        // Force sequence-style path expansion even for a one-frame selected
        // list so an output pattern such as frame_####.png is resolved.
        if (range_end == range_start) {
            ++range_end;
        }
        const FrameRange output_range{range_start, range_end, 1};

        for (const Frame frame : job.selected_frames) {
            if (!write_render_frame(*job.comp, renderer, frame, output_range,
                                    job.output, result.ok,
                                    result.telemetry_frames,
                                    result.total_render_ms,
                                    result.total_encode_ms,
                                    result.frames_written)) {
                // Continue to surface all selected-frame failures while
                // preserving the shared false result.
            }
        }
    } else {
        const std::int64_t start = job.mode == RenderMode::Still
            ? job.still_frame.integral()
            : job.first_frame.integral();
        const std::int64_t end = job.mode == RenderMode::Still
            ? job.still_frame.integral()
            : job.last_frame.integral();
        const std::int64_t step = std::max<std::int64_t>(
            1, job.frame_step.integral());

        const FrameRange range{start, end, step};
        for (std::int64_t f = start; f <= end; f += step) {
            if (!write_render_frame(*job.comp, renderer, Frame{f}, range,
                                    job.output, result.ok,
                                    result.telemetry_frames,
                                    result.total_render_ms,
                                    result.total_encode_ms,
                                    result.frames_written)) {
                // Continue to surface all frame failures while preserving false.
            }
        }
    }

    result.loop_end = profiling::now();
    return result;
}

} // namespace chronon3d::cli
