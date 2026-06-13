#include "../common/pipe_export_session.hpp"
#include "../common/pipe_export_helpers.hpp"

#include <spdlog/spdlog.h>
#include <chrono>

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
    // or corrupted. The caller (PipeExporter) relies on return_code for the
    // process exit code, so both success and return_code must reflect this.
    if (result.encoder_close_failed) {
        result.success = false;
    }

    // frames_written is the raw count from the render loop (status.frames_written),
    // but we report 0 on final failure (encoders close failure overrides render success)
    result.frames_written = result.success ? status.frames_written : 0;
    result.wall_time_ms = wall_time_ms;
    result.render_ms = render_ms;
    result.encode_ms = encode_ms;
    result.return_code = result.success ? 0 : 1;

    if (!result.success) {
        if (result.encoder_close_failed) {
            spdlog::error("[video] Export failed: encoder close failed after all frames rendered");
        }
        log_pipe_export_failure(status);
    } else {
        spdlog::info("[video] Wrote {}", session.opts.output);
    }

    return result;
}

} // namespace chronon3d::cli
