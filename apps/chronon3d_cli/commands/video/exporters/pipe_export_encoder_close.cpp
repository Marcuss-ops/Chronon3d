#include "../common/pipe_export_session.hpp"
#include "../common/pipe_export_helpers.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

EncoderCloseResult close_pipe_encoder(PipeExportSession& session) {
    EncoderCloseResult result;

    const bool is_native = (session.opts.encoder.encoder_backend == "native");
    result.write_blocked_ms = pipe_write_blocked_ms(is_native, *session.encoder);

    result.native_convert_ms   = session.encoder->native_convert_ms();
    result.native_send_ms      = session.encoder->native_send_frame_ms();
    result.native_receive_ms   = session.encoder->native_receive_packet_ms();
    result.native_mux_ms       = session.encoder->native_mux_write_ms();
    result.native_trailer_ms   = session.encoder->native_trailer_ms();

    const double conv_copy_ms = session.renderer && session.renderer->counters()
        ? static_cast<double>(session.renderer->counters()->frame_conversion_copy_ms.load())
        : 0.0;

    spdlog::info("[video] Encoder write blocked duration: {:.2f} ms", result.write_blocked_ms);
    spdlog::info("[video_diag] conversion_and_copy_duration_ms: {} ms", conv_copy_ms);

    if (is_native) {
        spdlog::info("[video_native] convert={:.2f}ms  send_frame={:.2f}ms  receive_packet={:.2f}ms  mux_write={:.2f}ms  trailer={:.2f}ms",
                     result.native_convert_ms, result.native_send_ms,
                     result.native_receive_ms, result.native_mux_ms, result.native_trailer_ms);
    }

    result.success = session.encoder->close();
    if (!result.success) {
        spdlog::error("[video] Encoder close failed");
    }

    return result;
}

} // namespace chronon3d::cli
