#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/output_contract.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>

namespace chronon3d::cli {

EncoderCloseResult close_pipe_encoder(PipeExportSession& session) {
    EncoderCloseResult result;

    const bool is_native = (session.opts.encoder.encoder_backend == "native");
    result.write_blocked_ms = pipe_write_blocked_ms(is_native, *session.encoder);

    const double conv_copy_ms = session.renderer_ptr() && session.renderer_ptr()->counters()
        ? static_cast<double>(session.renderer_ptr()->counters()->frame_conversion_copy_wall_ms.load())
        : 0.0;
    const auto conversion_bytes = session.renderer_ptr() && session.renderer_ptr()->counters()
        ? session.renderer_ptr()->counters()->conversion_bytes_written.load()
        : 0ULL;
    const auto staging_copy_bytes = session.renderer_ptr() && session.renderer_ptr()->counters()
        ? session.renderer_ptr()->counters()->encoder_staging_copy_bytes.load()
        : 0ULL;
    const auto encoder_slot_reuses = session.renderer_ptr() && session.renderer_ptr()->counters()
        ? session.renderer_ptr()->counters()->encoder_slot_reuses.load()
        : 0ULL;

    spdlog::info("[video] Encoder write blocked duration: {:.2f} ms", result.write_blocked_ms);
    spdlog::info("[video_diag] conversion_and_copy_duration_ms: {} ms", conv_copy_ms);
    spdlog::info(
        "[video_diag] conversion_bytes_written={} encoder_staging_copy_bytes={} encoder_slot_reuses={}",
        conversion_bytes, staging_copy_bytes, encoder_slot_reuses);

    result.success = session.encoder->close();
    if (!result.success) spdlog::error("[video] Encoder close failed");

    result.native_convert_ms      = session.encoder->native_convert_ms();
    result.native_send_ms         = session.encoder->native_send_frame_ms();
    result.native_backpressure_ms = session.encoder->native_backpressure_ms();
    result.native_cuda_pending_peak = session.encoder->native_cuda_pending_peak();
    result.native_cuda_backpressure_wait_count =
        session.encoder->native_cuda_backpressure_wait_count();
    result.native_flush_ms        = session.encoder->native_flush_ms();
    result.native_receive_ms      = session.encoder->native_receive_packet_ms();
    result.native_mux_ms          = session.encoder->native_mux_write_ms();
    result.native_trailer_ms      = session.encoder->native_trailer_ms();
    result.encoder_hwframe_get_buffer_ms = session.encoder->encoder_hwframe_get_buffer_ms();
    result.encoder_surface_acquire_ms    = session.encoder->encoder_surface_acquire_ms();
    result.encoder_nvenc_submit_ms       = session.encoder->encoder_nvenc_submit_ms();
    result.encoder_queue_backpressure_wait_ms = session.encoder->encoder_queue_backpressure_wait_ms();
    result.encoder_packet_drain_ms       = session.encoder->encoder_packet_drain_ms();
    result.direct_yuv_cuda_launch_ms     = session.encoder->direct_yuv_cuda_launch_ms();
    result.direct_yuv_cuda_wait_ms       = session.encoder->direct_yuv_cuda_wait_ms();
    result.applied_encoder_preset        = session.encoder->applied_encoder_preset();
    result.applied_encoder_rate_control  = session.encoder->applied_encoder_rate_control();
    result.applied_encoder_async_depth   = session.encoder->applied_encoder_async_depth();

    if (is_native) {
        spdlog::info(
            "[video_native] convert={:.2f}ms  send_frame={:.2f}ms  backpressure={:.2f}ms  "
            "flush={:.2f}ms  receive_packet={:.2f}ms  mux_write={:.2f}ms  trailer={:.2f}ms",
            result.native_convert_ms, result.native_send_ms, result.native_backpressure_ms,
            result.native_flush_ms, result.native_receive_ms, result.native_mux_ms,
            result.native_trailer_ms);
    }

    return result;
}

PipeExportResult make_pipe_export_result(
    const PipeExportSession& session,
    const RenderLoopResult& loop_result,
    const EncoderCloseResult& close_result,
    double render_ms,
    double encode_ms,
    double wall_time_ms)
{
    PipeExportResult result;
    const auto& status = loop_result.status;

    result.success = status.success;
    result.cancelled = status.cancelled;
    result.render_failed = status.render_failed;
    result.writer_error = status.writer_error;
    result.exception_error = status.exception_error;
    result.encoder_close_failed = !close_result.success;
    if (result.encoder_close_failed) result.success = false;

    result.frames_rendered = status.frames_rendered;
    result.frames_enqueued = status.frames_enqueued;
    result.frames_encoded = status.frames_encoded;
    result.wall_time_ms = wall_time_ms;
    result.render_ms = render_ms;
    result.encode_ms = encode_ms;
    result.return_code = result.success ? 0 : 1;

    const auto partial_path = std::filesystem::path(session.opts.output.output);
    const auto final_path = std::filesystem::path(session.original_output_path);

    if (!result.success) {
        if (result.encoder_close_failed) {
            spdlog::error("[video] Export failed: encoder close failed after all frames rendered");
        }
        log_pipe_export_failure(status);
        std::error_code ec;
        if (std::filesystem::exists(partial_path, ec)) {
            std::filesystem::remove(partial_path, ec);
            if (ec) {
                spdlog::warn("[video] Failed to remove partial output {}: {}",
                             partial_path.string(), ec.message());
            }
        }
    } else {
        const auto validation_t0 = profiling::now();
        auto contract_result = media::video::resolve_output_contract("youtube_overlay_v1");
        if (!contract_result) {
            spdlog::error("[video] output contract unavailable: {}",
                          std::move(contract_result).error());
            result.success = false;
            result.return_code = 1;
            std::error_code ec;
            std::filesystem::remove(partial_path, ec);
            return result;
        }
        auto contract = std::move(contract_result).value();
        contract.width = session.canvas_width;
        contract.height = session.canvas_height;
        contract.fps = session.opts.output.frame_rate();
        contract.frame_count = session.total_frames;
        contract.audio_required = session.opts.encoder.encoder_backend == "native" &&
            !session.opts.gop_source.empty();
        contract.audio_streams = contract.audio_required ? 1 : 0;

        const auto verification = media::video::verify_output_contract(partial_path, contract);
        result.validation_ms = profiling::duration_ms(validation_t0, profiling::now());
        result.sha256 = verification.sha256;
        result.ffprobe_ms = verification.ffprobe_ms;
        result.sha256_ms = verification.sha256_ms;
        result.copy_eligible = verification.copy_eligible;

        if (!verification.passed) {
            spdlog::error("[video] output verification failed — {}", verification.failure);
            result.success = false;
            result.return_code = 1;
            std::error_code ec;
            std::filesystem::remove(partial_path, ec);
            return result;
        }
        if (!verification.copy_eligible) {
            spdlog::warn("[video] artifact decodable but not copy-eligible — {}",
                         verification.failure);
        } else {
            spdlog::info("[video] copy_eligible=true sha256={}", result.sha256);
        }

        const auto output_t0 = profiling::now();
        std::error_code ec;
        std::filesystem::rename(partial_path, final_path, ec);
        result.output_finalize_ms = profiling::duration_ms(output_t0, profiling::now());
        if (ec) {
            spdlog::error("[video] Failed to rename {} → {}: {}",
                         partial_path.string(), final_path.string(), ec.message());
            result.success = false;
            result.return_code = 1;
            std::filesystem::remove(partial_path, ec);
        } else {
            result.output_published = true;
            spdlog::info("[video] Wrote {}", session.original_output_path);
        }
    }

    return result;
}

} // namespace chronon3d::cli
