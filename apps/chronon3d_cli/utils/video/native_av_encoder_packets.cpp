#include "native_av_encoder.hpp"
#include "gop_smart_copy.hpp"
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

bool NativeAvEncoder::submit_copied_video(
    AVPacket& packet, AVRational source_time_base) {
    std::lock_guard lock(mutex_);
    if (!open_complete_ || !mux_ || !packet.data) {
        spdlog::error("[native_av] copied packet submitted outside an open mux session");
        return false;
    }
    const auto started = Clock::now();
    const bool ok = mux_->submit_video(packet, source_time_base);
    native_mux_write_ms_ += elapsed_ms(started);
    return ok;
}

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

        ++encoder_packets_received_;
        if (packet_->pts != AV_NOPTS_VALUE) {
            if (first_encoder_packet_pts_ < 0) {
                first_encoder_packet_pts_ = packet_->pts;
            }
            last_encoder_packet_pts_ = packet_->pts;
        }

        // P0-5E — capture the encoder's OWN output facts from the first packet
        // that publishes them (libx264 and NVENC keep their parameter sets
        // in-band, so the codec context never carries them). This is the
        // contract the borrowed-mux splice gate certifies against.
        if (!bitstream_contract_.encoder_materialized &&
            resolved_encoder_config_) {
            if (auto published = encoder_bitstream_contract_from_packet(
                    *packet_, *codec_, options_, *resolved_encoder_config_)) {
                bitstream_contract_ = std::move(*published);
                spdlog::info(
                    "[native_av] encoder bitstream contract materialized from "
                    "the produced packet: profile={} level={} pix_fmt={} "
                    "parameter_sets={}",
                    bitstream_contract_.profile, bitstream_contract_.level,
                    static_cast<int>(bitstream_contract_.pixel_format),
                    bitstream_contract_.parameter_sets.size());
            }
        }

        // P0-5E — emit the format the borrowed container STORES.
        //
        // This encoder produces Annex-B, and libavformat converts that to the
        // length-prefixed samples an mp4 stream carries only while the output
        // stream has no extradata. A container declared from a compressed
        // source (the whole point of this lane) already carries that source's
        // configuration record, so nothing would convert the packets and the
        // spliced output would not decode. The format conversion therefore
        // belongs to the producer that knows both sides: here, right before the
        // packet is handed to the mux that owns the container.
        if (borrows_mux_ && options_.borrowed_declared_stream) {
            const auto& declared = *options_.borrowed_declared_stream;
            if (parameter_sets_are_config_record(
                    declared.codec, declared.parameter_sets.data(),
                    declared.parameter_sets.size()) &&
                convert_annex_b_to_length_prefixed(*packet_, declared.codec)) {
                spdlog::debug(
                    "[native_av] borrowed container stores length-prefixed "
                    "samples; converted the encoded packet into the declared "
                    "format ({} bytes)",
                    packet_->size);
            }
        }

        const auto t_mux0 = Clock::now();
        // Move-only packet ownership: MuxSession consumes the packet inside
        // submit() and never retains it, so no shared_ptr control block is
        // needed on this path.
        media::OwnedAVPacket owned_packet(av_packet_alloc());
        if (!owned_packet || av_packet_ref(owned_packet.get(), packet_) < 0) {
            ret = AVERROR(ENOMEM);
        } else {
            // P0-5E: on a borrowed container this encode belongs at the tick
            // the schedule reserved for the work unit being rendered, so the
            // packet is placed at the container's scheduled origin. On an
            // owned container the packet's own timestamps are the truth.
            ret = mux_->submit(
                      media::EncodedPacket{
                          std::move(owned_packet), codec_->time_base,
                          (packet_->flags & AV_PKT_FLAG_KEY) != 0},
                      borrows_mux_ ? media::MuxVideoPlacement::ScheduledOrigin
                                   : media::MuxVideoPlacement::AsSubmitted)
                ? 0
                : AVERROR_EXTERNAL;
        }
        const double mux_ms = elapsed_ms(t_mux0);
        native_mux_write_ms_ += mux_ms;

        av_packet_unref(packet_);

        if (ret < 0) {
            spdlog::error("[native_av] av_interleaved_write_frame error: {}", ret);
            return false;
        }
        ++mux_packets_written_;

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
                AVPacket* copy = av_packet_alloc();
                if (!copy || av_packet_ref(copy, packet) < 0) {
                    if (copy) av_packet_free(&copy);
                    ok = false;
                    av_packet_unref(packet);
                    break;
                }
                media::OwnedAVPacket owned(copy);
                if (!mux_->submit_audio(media::EncodedPacket{
                        std::move(owned), time_base, false})) {
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
