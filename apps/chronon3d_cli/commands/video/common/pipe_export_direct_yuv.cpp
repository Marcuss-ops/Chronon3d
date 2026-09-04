#include "pipe_export_session.hpp"
#include "pipe_export_helpers.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/direct_yuv_executor.hpp>

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>

namespace chronon3d::cli {

RenderLoopOutput run_direct_yuv_loop(
    PipeExportSession& session,
    media::NativeVideoFrameDecoder& decoder,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts) {
    RenderLoopOutput output;
    auto& status = output.loop_result.status;

    if (!session.direct_yuv_session || !session.direct_yuv_session->executor) {
        mark_pipe_render_failed(status, start);
        output.render_end = profiling::now();
        return output;
    }

    media::video::DirectYuvExecutionRequest request;
    request.start = start;
    request.end = end;
    request.cancellation_token = opts.cancellation_token;
    request.consumer_failed = [&session]() {
        return session.writer_failed.load(std::memory_order_relaxed);
    };
    request.submit = [&session, &opts](
        Frame frame, std::shared_ptr<media::video::DirectYuvFrame> direct_frame) {
        RenderFramePackage package = DirectYuvFramePackage{
            .frame_number = frame,
            .direct_yuv = std::move(direct_frame)};
        return session.queue.push(package, opts.cancellation_token);
    };
    request.progress = [](int done, int total) {
        if (should_log_pipe_progress(done, total)) {
            spdlog::info("[video]   {}/{} frames", done, total);
        }
    };

    const auto media_result =
        session.direct_yuv_session->executor->run(decoder, request);

    status.frames_rendered = media_result.frames_rendered;
    status.frames_enqueued = media_result.frames_submitted;
    output.loop_result.direct_yuv_execute_ms = media_result.execute_ms;
    output.loop_result.queue_wait_ms = media_result.submit_wait_ms;
    output.render_ms = media_result.wall_ms;
    output.render_end = profiling::now();
    session.direct_yuv_session->counters.io_queue_push_wait_ms.fetch_add(
        static_cast<std::uint64_t>(media_result.submit_wait_ms),
        std::memory_order_relaxed);

    output.telemetry_frames.reserve(media_result.timings.size());
    for (const auto& timing : media_result.timings) {
        output.telemetry_frames.push_back({
            .frame_number = static_cast<int>(timing.frame),
            .wall_start_ms = timing.wall_start_ms,
            .duration_ms = timing.execute_ms + timing.submit_wait_ms,
            .cache_hit = true,
            .dirty_area_ratio = 0.0,
            .node_lookup_ms = 0.0,
            .graph_eval_ms = 0.0,
            .direct_yuv_decode_ms = timing.execute_ms,
            .queue_wait_ms = timing.submit_wait_ms,
            .render_breakdown = {},
            .image_timing = {},
            .text_timing = {},
            .dirty_rect_enabled = false,
            .dirty_rect_x0 = 0,
            .dirty_rect_y0 = 0,
            .dirty_rect_x1 = 0,
            .dirty_rect_y1 = 0,
            .tile_execution_used = false,
            .fast_path_reused = true,
            .graph_reused = true,
            .program_cache_capacity = 1});
    }

    switch (media_result.status) {
        case media::video::DirectYuvExecutionStatus::Success:
            status.success = true;
            break;
        case media::video::DirectYuvExecutionStatus::Cancelled:
            mark_pipe_cancelled(status, media_result.terminal_frame);
            break;
        case media::video::DirectYuvExecutionStatus::ConsumerFailed:
            mark_pipe_writer_failed(status, media_result.terminal_frame);
            break;
        case media::video::DirectYuvExecutionStatus::ExecutionFailed:
        case media::video::DirectYuvExecutionStatus::SubmitFailed:
            mark_pipe_render_failed(status, media_result.terminal_frame);
            break;
        case media::video::DirectYuvExecutionStatus::Exception: {
            const std::runtime_error error(
                media_result.error.empty()
                    ? "DirectYUV media executor failed"
                    : media_result.error);
            mark_pipe_exception(status, media_result.terminal_frame, error);
            break;
        }
    }

    return output;
}

} // namespace chronon3d::cli
