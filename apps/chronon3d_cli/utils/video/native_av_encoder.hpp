#pragma once

#include "ffmpeg_pipe_encoder.hpp"
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
}

namespace chronon3d::cli {

/// Native libavcodec/libavformat encoder.
///
/// Encodes frames directly via avcodec_send_frame / avcodec_receive_packet
/// and muxes to MP4 via avformat, eliminating the external ffmpeg subprocess
/// and pipe overhead entirely.
class NativeAvEncoder : public IVideoEncoder {
public:
    NativeAvEncoder() = default;
    ~NativeAvEncoder() override;

    NativeAvEncoder(const NativeAvEncoder&) = delete;
    NativeAvEncoder& operator=(const NativeAvEncoder&) = delete;
    NativeAvEncoder(NativeAvEncoder&&) = delete;
    NativeAvEncoder& operator=(NativeAvEncoder&&) = delete;

    bool open(const FfmpegPipeOptions& options) override;
    bool write_frame(const Framebuffer& fb) override;
    bool close() override;

    [[nodiscard]] uint64_t frames_written() const override { return frames_written_; }

    // ── Native encoder telemetry accessors ──
    [[nodiscard]] double native_convert_ms()          const override { return native_convert_ms_; }
    [[nodiscard]] double native_send_frame_ms()       const override { return native_send_frame_ms_; }
    [[nodiscard]] double native_receive_packet_ms()   const override { return native_receive_packet_ms_; }
    [[nodiscard]] double native_mux_write_ms()        const override { return native_mux_write_ms_; }
    [[nodiscard]] double native_trailer_ms()          const override { return native_trailer_ms_; }

private:
    FfmpegPipeOptions options_{};
    uint64_t frames_written_{0};

    // FFmpeg objects
    AVFormatContext* fmt_{nullptr};
    AVCodecContext*  codec_{nullptr};
    AVStream*        stream_{nullptr};
    AVFrame*         frame_{nullptr};
    AVPacket*        packet_{nullptr};

    // ── Telemetry accumulators (ms) ──
    double native_convert_ms_{0.0};
    double native_send_frame_ms_{0.0};
    double native_receive_packet_ms_{0.0};
    double native_mux_write_ms_{0.0};
    double native_trailer_ms_{0.0};

    /// Drain all pending packets from the encoder after avcodec_send_frame.
    bool drain_packets();
};

} // namespace chronon3d::cli
