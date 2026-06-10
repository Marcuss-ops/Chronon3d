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
    result.frames_written = pipe_encoded_frame_count(status);
    result.wall_time_ms = wall_time_ms;
    result.render_ms = render_ms;
    result.encode_ms = encode_ms;
    result.return_code = status.success ? 0 : 1;

    if (!status.success) {
        log_pipe_export_failure(status);
    } else {
        spdlog::info("[video] Wrote {}", session.opts.output);
    }

    return result;
}

} // namespace chronon3d::cli
