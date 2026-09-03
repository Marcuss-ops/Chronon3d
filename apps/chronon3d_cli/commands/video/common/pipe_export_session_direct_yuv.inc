RenderLoopOutput run_direct_yuv_loop(
    PipeExportSession& session,
    media::NativeVideoFrameDecoder& decoder,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts) {
    RenderLoopOutput output;
    auto& result = output.loop_result;
    auto& status = result.status;
    const int total = static_cast<int>(end - start);
    Frame current_frame = start;
    const auto loop_t0 = profiling::now();
    output.telemetry_frames.reserve(total > 0 ? static_cast<std::size_t>(total) : 0U);

    try {
        for (; current_frame < end; ++current_frame) {
            if (opts.cancellation_token && opts.cancellation_token->is_cancelled()) {
                mark_pipe_cancelled(status, current_frame);
                break;
            }
            if (session.writer_failed.load(std::memory_order_relaxed)) {
                mark_pipe_writer_failed(status, current_frame);
                break;
            }

            const auto direct_t0 = profiling::now();
            auto direct_frame = session.direct_yuv_selected()
                ? session.direct_yuv_session->program->execute(decoder, current_frame)
                : nullptr;
            const auto direct_t1 = profiling::now();
            const double frame_ms = profiling::duration_ms(direct_t0, direct_t1);
            result.direct_yuv_execute_ms += frame_ms;
            ++status.frames_rendered;
            if (!direct_frame) {
                mark_pipe_render_failed(status, current_frame);
                break;
            }

            RenderFramePackage package = DirectYuvFramePackage{
                .frame_number = current_frame,
                .direct_yuv = std::move(direct_frame)};
            const auto queue_t0 = profiling::now();
            const bool pushed = session.queue.push(package, opts.cancellation_token);
            const double wait_ms = profiling::duration_ms(queue_t0, profiling::now());
            result.queue_wait_ms += wait_ms;
            session.direct_yuv_session->counters.io_queue_push_wait_ms.fetch_add(
                static_cast<std::uint64_t>(wait_ms), std::memory_order_relaxed);
            if (!pushed) {
                if (session.writer_failed.load(std::memory_order_relaxed))
                    mark_pipe_writer_failed(status, current_frame);
                else
                    mark_pipe_render_failed(status, current_frame);
                break;
            }
            ++status.frames_enqueued;
            const int done_count = static_cast<int>(current_frame - start + 1);
            if (should_log_pipe_progress(done_count, total)) {
                spdlog::info("[video]   {}/{} frames", done_count, total);
            }
            output.telemetry_frames.push_back({
                .frame_number = static_cast<int>(current_frame),
                .wall_start_ms = profiling::duration_ms(loop_t0, direct_t0),
                .duration_ms = frame_ms + wait_ms,
                .cache_hit = true,
                .dirty_area_ratio = 0.0,
                .node_lookup_ms = 0.0,
                .graph_eval_ms = 0.0,
                .direct_yuv_decode_ms = frame_ms,
                .queue_wait_ms = wait_ms,
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
    } catch (const std::exception& error) {
        mark_pipe_exception(status, current_frame, error);
    }
    finalize_render_session(status, current_frame, end);
    output.render_end = profiling::now();
    output.render_ms = profiling::duration_ms(loop_t0, output.render_end);
    return output;
}
