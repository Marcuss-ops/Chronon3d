#include "pipe_export_session.hpp"
#include "pipe_export_helpers.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

#include <spdlog/spdlog.h>

#include <chrono>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chronon3d::cli {

RenderLoopOutput run_direct_yuv_loop(
    PipeExportSession& session,
    media::NativeVideoFrameDecoder& decoder,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts) {
    RenderLoopOutput output;
    auto& status = output.loop_result.status;

    if (!session.direct_yuv_session || !session.direct_yuv_session->program) {
        mark_pipe_render_failed(status, start);
        output.render_end = profiling::now();
        return output;
    }

    struct FrameTiming {
        Frame frame{0};
        double wall_start_ms{0.0};
        double execute_ms{0.0};
        double submit_wait_ms{0.0};
    };

    const int total = static_cast<int>((end - start).integral());
    std::vector<FrameTiming> timings;
    if (total > 0) timings.reserve(static_cast<std::size_t>(total));
    const auto loop_start = std::chrono::steady_clock::now();
    double execute_total_ms = 0.0;
    double submit_total_ms = 0.0;
    bool completed = true;

    try {
        for (Frame frame = start; frame < end; ++frame) {
            if (opts.cancellation_token && opts.cancellation_token->is_cancelled()) {
                mark_pipe_cancelled(status, frame);
                completed = false;
                break;
            }
            if (session.writer_failed.load(std::memory_order_relaxed)) {
                mark_pipe_writer_failed(status, frame);
                completed = false;
                break;
            }

            FrameTiming timing;
            timing.frame = frame;
            const auto execute_start = std::chrono::steady_clock::now();
            timing.wall_start_ms = std::chrono::duration<double, std::milli>(
                execute_start - loop_start).count();
            auto direct_frame = session.direct_yuv_session->program->execute(decoder, frame);
            const auto execute_end = std::chrono::steady_clock::now();
            timing.execute_ms = std::chrono::duration<double, std::milli>(
                execute_end - execute_start).count();
            execute_total_ms += timing.execute_ms;
            ++status.frames_rendered;

            if (!direct_frame) {
                mark_pipe_render_failed(status, frame);
                completed = false;
                break;
            }

            const auto submit_start = std::chrono::steady_clock::now();
            RenderFramePackage package = DirectYuvFramePackage{
                .frame_number = frame,
                .direct_yuv = std::move(direct_frame)};
            const bool accepted = session.queue.push(package, opts.cancellation_token);
            const auto submit_end = std::chrono::steady_clock::now();
            timing.submit_wait_ms = std::chrono::duration<double, std::milli>(
                submit_end - submit_start).count();
            submit_total_ms += timing.submit_wait_ms;

            if (!accepted) {
                if (session.writer_failed.load(std::memory_order_relaxed)) {
                    mark_pipe_writer_failed(status, frame);
                } else if (opts.cancellation_token && opts.cancellation_token->is_cancelled()) {
                    mark_pipe_cancelled(status, frame);
                } else {
                    mark_pipe_render_failed(status, frame);
                }
                completed = false;
                break;
            }

            ++status.frames_enqueued;
            timings.push_back(timing);
            const int done = static_cast<int>((frame - start).integral() + 1);
            if (should_log_pipe_progress(done, total)) {
                spdlog::info("[video]   {}/{} frames", done, total);
            }
        }
    } catch (const std::exception& error) {
        mark_pipe_exception(status, start + Frame{status.frames_rendered}, error);
        completed = false;
    } catch (...) {
        const std::runtime_error error("unknown DirectYUV execution exception");
        mark_pipe_exception(status, start + Frame{status.frames_rendered}, error);
        completed = false;
    }

    if (completed) status.success = true;
    output.loop_result.direct_yuv_execute_ms = execute_total_ms;
    output.loop_result.queue_wait_ms = submit_total_ms;
    output.render_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - loop_start).count();
    output.render_end = profiling::now();
    spdlog::info(
        "[direct-yuv] loop finished: frames={} total_render={:.2f}ms template_evals={} template_reuses={}",
        status.frames_rendered, output.render_ms,
        session.direct_yuv_session->program->template_eval_count(),
        session.direct_yuv_session->program->template_reuse_count());
    session.direct_yuv_session->counters.io_queue_push_wait_ms.fetch_add(
        static_cast<std::uint64_t>(submit_total_ms), std::memory_order_relaxed);

    output.telemetry_frames.reserve(timings.size());
    for (const auto& timing : timings) {
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
    return output;
}

} // namespace chronon3d::cli
