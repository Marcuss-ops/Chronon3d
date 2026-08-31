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
    if (!codec_ || !mux_ || !packet_) {
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

        const auto t_mux0 = Clock::now();
        auto owned_packet = std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* value) {
            if (value) av_packet_free(&value);
        });
        if (!owned_packet || av_packet_ref(owned_packet.get(), packet_) < 0) {
            ret = AVERROR(ENOMEM);
        } else {
            ret = mux_->submit(media::EncodedPacket{
                std::move(owned_packet), codec_->time_base,
                (packet_->flags & AV_PKT_FLAG_KEY) != 0}) ? 0 : AVERROR_EXTERNAL;
        }
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

bool NativeAvEncoder::mux_source_audio() {
    if (!audio_input_ || audio_input_stream_ < 0 || !mux_ || !mux_->has_audio()) {
        return false;
    }
    const AVRational time_base = audio_input_->streams[audio_input_stream_]->time_base;
    const auto to_pts = [time_base](double seconds) {
        return static_cast<std::int64_t>(std::llround(seconds / av_q2d(time_base)));
    };
    const std::int64_t start_pts = to_pts(options_.audio_start_seconds);
    const std::int64_t end_pts = options_.audio_end_seconds > options_.audio_start_seconds
        ? to_pts(options_.audio_end_seconds) : AV_NOPTS_VALUE;
    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;
    bool ok = true;
    while (av_read_frame(audio_input_, packet) >= 0) {
        if (packet->stream_index == audio_input_stream_) {
            const std::int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
            if (pts != AV_NOPTS_VALUE && pts >= start_pts &&
                (end_pts == AV_NOPTS_VALUE || pts < end_pts)) {
                if (packet->pts != AV_NOPTS_VALUE) packet->pts -= start_pts;
                if (packet->dts != AV_NOPTS_VALUE) packet->dts -= start_pts;
                if (!mux_->submit_audio({
                        std::shared_ptr<AVPacket>(packet, [](AVPacket*) {}),
                        time_base, false})) {
                    ok = false;
                    av_packet_unref(packet);
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&audio_input_);
    audio_input_stream_ = -1;
    return ok;
}


} // namespace chronon3d::cli
