#include "../common/pipe_export_session.hpp"
#include "../common/pipe_export_helpers.hpp"

#include <spdlog/spdlog.h>
#include <chrono>
#include <filesystem>

namespace chronon3d::cli {

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

    // An encoder close failure overrides success: even if all frames were
    // rendered and encoded, a failed close means the output is incomplete
    // or corrupted. The caller relies on return_code for the process exit
    // code, so both success and return_code must reflect this.
    if (result.encoder_close_failed) {
        result.success = false;
    }

    result.frames_rendered = status.frames_rendered;
    result.frames_enqueued = status.frames_enqueued;
    result.frames_encoded = status.frames_encoded;
    result.wall_time_ms = wall_time_ms;
    result.render_ms = render_ms;
    result.encode_ms = encode_ms;
    result.return_code = result.success ? 0 : 1;

    // P1-B: atomic output — FFmpeg wrote to session.opts.output.output
    // (which has .partial suffix).  On success, rename to the original
    // final path.  On failure, clean up the partial file.
    const auto partial_path = std::filesystem::path(session.opts.output.output);
    const auto final_path = std::filesystem::path(session.original_output_path);

    if (!result.success) {
        if (result.encoder_close_failed) {
            spdlog::error("[video] Export failed: encoder close failed after all frames rendered");
        }
        log_pipe_export_failure(status);
        // Clean up partial output on failure
        std::error_code ec;
        if (std::filesystem::exists(partial_path, ec)) {
            std::filesystem::remove(partial_path, ec);
            if (ec) {
                spdlog::warn("[video] Failed to remove partial output {}: {}",
                             partial_path.string(), ec.message());
            }
        }
    } else {
        // P1-B: ffprobe validation before rename
        // Verify the .partial file is a valid video with correct stream,
        // resolution, fps, duration, and non-zero file size.
        const bool valid = validate_video_output(
            session.opts.output.output,
            session.canvas_width, session.canvas_height,
            session.opts.output.fps, session.total_frames);
        if (!valid) {
            spdlog::error("[video] ffprobe validation failed — output may be corrupt");
            result.success = false;
            result.return_code = 1;
            std::error_code ec;
            std::filesystem::remove(partial_path, ec);
            return result;
        }

        // Atomic rename: .partial → final path
        std::error_code ec;
        std::filesystem::rename(partial_path, final_path, ec);
        if (ec) {
            spdlog::error("[video] Failed to rename {} → {}: {}",
                         partial_path.string(), final_path.string(), ec.message());
            result.success = false;
            result.return_code = 1;
            // Clean up partial on rename failure too
            std::filesystem::remove(partial_path, ec);
        } else {
            result.output_published = true;
            spdlog::info("[video] Wrote {}", session.original_output_path);
        }
    }

    return result;
}

} // namespace chronon3d::cli
