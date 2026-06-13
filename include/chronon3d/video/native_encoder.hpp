#pragma once

// =============================================================================
// include/chronon3d/video/native_encoder.hpp — In-process libav encoder
//
// Part of chronon3d_backend_video.  Only compiled when
// CHRONON3D_ENABLE_NATIVE_FFMPEG is defined.
// =============================================================================

#include "encoder.hpp"
#include <string>
#include <vector>
#include <cstdint>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
}
#endif

namespace chronon3d::video {

/// Native libavcodec/libavformat encoder.
///
/// Encodes frames directly via avcodec_send_frame / avcodec_receive_packet
/// and muxes to MP4 via avformat, eliminating the external ffmpeg subprocess
/// and pipe overhead entirely.
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
class NativeAvEncoder : public IVideoEncoder {
#else
class NativeAvEncoder final : public IVideoEncoder {
#endif
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
    [[nodiscard]] EncoderFrameTelemetry last_frame_telemetry() const override { return last_frame_telemetry_; }

    // ── Native encoder telemetry accessors ──
    [[nodiscard]] double native_convert_ms()          const override { return native_convert_ms_; }
    [[nodiscard]] double native_send_frame_ms()       const override { return native_send_frame_ms_; }
    [[nodiscard]] double native_receive_packet_ms()   const override { return native_receive_packet_ms_; }
    [[nodiscard]] double native_mux_write_ms()        const override { return native_mux_write_ms_; }
    [[nodiscard]] double native_trailer_ms()          const override { return native_trailer_ms_; }

private:
    FfmpegPipeOptions options_{};
    uint64_t frames_written_{0};

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    // FFmpeg objects
    AVFormatContext* fmt_{nullptr};
    AVCodecContext*  codec_{nullptr};
    AVStream*        stream_{nullptr};
    AVFrame*         frame_{nullptr};
    AVPacket*        packet_{nullptr};
#endif

    // ── Telemetry accumulators (ms) ──
    double native_convert_ms_{0.0};
    double native_send_frame_ms_{0.0};
    double native_receive_packet_ms_{0.0};
    double native_mux_write_ms_{0.0};
    double native_trailer_ms_{0.0};
    EncoderFrameTelemetry last_frame_telemetry_{};

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    bool drain_packets();
#endif

    // ── Single-entry YUV conversion cache ──
    uint64_t last_converted_digest_{0};
    int      last_converted_width_{0};
    int      last_converted_height_{0};
    int      last_converted_pix_fmt_{-1};
    bool     last_converted_apply_gamma_{false};
    int      last_converted_color_matrix_{-1};
    uint64_t cache_hits_{0};
    uint64_t cache_misses_{0};
};

} // namespace chronon3d::video
