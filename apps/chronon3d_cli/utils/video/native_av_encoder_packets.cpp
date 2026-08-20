#include "native_av_encoder.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

namespace chronon3d::cli {
namespace {
using Clock = std::chrono::steady_clock;
double elapsed_ms(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
} // namespace

// ---------------------------------------------------------------------------
// drain_packets — common helper used during write and close
// ---------------------------------------------------------------------------

bool NativeAvEncoder::drain_packets() {
    if (!codec_ || !fmt_ || !stream_ || !packet_) {
        return false;
    }

    for (;;) {
        const auto t_recv0 = Clock::now();
        int ret = avcodec_receive_packet(codec_, packet_);
        const double recv_ms = elapsed_ms(t_recv0);
        native_receive_packet_ms_ += recv_ms;

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No more packets available right now (or stream fully drained)
            return true;
        }
        if (ret < 0) {
            spdlog::error("[native_av] avcodec_receive_packet error: {}", ret);
            return false;
        }

        // libx264 emits PTS but no per-packet duration; the MP4 muxer
        // derives the container duration from the last packet's
        // pts + duration, so without an explicit duration the final
        // frame's display time is dropped (a 90-frame @30fps segment
        // reports 2.967s instead of 3.0s — Test 7).  One frame in codec
        // time_base {1, fps} units.
        if (packet_->duration <= 0) {
            packet_->duration = 1;
        }

        // Rescale timestamps to stream time base and mux
        av_packet_rescale_ts(packet_, codec_->time_base, stream_->time_base);
        packet_->stream_index = stream_->index;

        const auto t_mux0 = Clock::now();
        ret = av_interleaved_write_frame(fmt_, packet_);
        const double mux_ms = elapsed_ms(t_mux0);
        native_mux_write_ms_ += mux_ms;

        av_packet_unref(packet_);

        if (ret < 0) {
            spdlog::error("[native_av] av_interleaved_write_frame error: {}", ret);
            return false;
        }

        if (profiling::g_current_counters) {
            profiling::g_current_counters->native_av_receive_packet_wall_ms.fetch_add(
                static_cast<uint64_t>(recv_ms), std::memory_order_relaxed);
            profiling::g_current_counters->native_av_mux_write_wall_ms.fetch_add(
                static_cast<uint64_t>(mux_ms), std::memory_order_relaxed);
        }
    }
}


} // namespace chronon3d::cli
