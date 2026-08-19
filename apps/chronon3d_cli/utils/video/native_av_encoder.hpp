#pragma once

#include "ffmpeg_pipe_encoder.hpp"
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#endif


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
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
    void set_counters(chronon3d::RenderCounters* counters) override {
        counters_ = counters;
    }
    bool write_frame(const Framebuffer& fb) override;
    bool write_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination) override;
    bool close() override;

    [[nodiscard]] uint64_t frames_written() const override { return frames_written_; }
    [[nodiscard]] EncoderFrameTelemetry last_frame_telemetry() const override { return last_frame_telemetry_; }

    // ── Native encoder telemetry accessors ──
    [[nodiscard]] double native_convert_ms()          const override { return native_convert_ms_; }
    [[nodiscard]] double native_send_frame_ms()       const override { return native_send_frame_ms_; }
    [[nodiscard]] double native_backpressure_ms()     const override { return native_backpressure_ms_; }
    [[nodiscard]] std::uint64_t native_cuda_pending_peak() const override {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        return cuda_pending_peak_;
#else
        return 0;
#endif
    }
    [[nodiscard]] std::uint64_t native_cuda_backpressure_wait_count() const override {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        return cuda_backpressure_wait_count_;
#else
        return 0;
#endif
    }
    [[nodiscard]] double native_flush_ms()            const override { return native_flush_ms_; }
    [[nodiscard]] double native_receive_packet_ms()   const override { return native_receive_packet_ms_; }
    [[nodiscard]] double native_mux_write_ms()        const override { return native_mux_write_ms_; }
    [[nodiscard]] double native_trailer_ms()          const override { return native_trailer_ms_; }

private:
    chronon3d::RenderCounters* counters_{nullptr};
    FfmpegPipeOptions options_{};
    uint64_t frames_written_{0};

    // FFmpeg objects
    AVFormatContext* fmt_{nullptr};
    AVCodecContext*  codec_{nullptr};
    AVStream*        stream_{nullptr};
    AVFrame*         frame_{nullptr};
    AVPacket*        packet_{nullptr};

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    using CudaSurfaceBridge = backends::vulkan::CudaVulkanSurfaceBridge;
    std::unordered_map<std::uint64_t, std::unique_ptr<CudaSurfaceBridge>> cuda_surface_bridges_;
    struct PendingCudaFrame {
        AVFrame* frame{nullptr};
        CUevent ready{nullptr};
    };
    std::deque<PendingCudaFrame> pending_cuda_frames_;
    CUstream cuda_stream_{nullptr};
    uint64_t frames_submitted_{0};
    uint64_t cuda_pending_peak_{0};
    uint64_t cuda_backpressure_wait_count_{0};
    void* cuda_context_{nullptr};
    AVBufferRef* cuda_device_ref_{nullptr};
    AVBufferRef* cuda_frames_ref_{nullptr};
#endif
    bool gpu_nvenc_{false};
    bool open_complete_{false};

    // ── Telemetry accumulators (ms) ──
    double native_convert_ms_{0.0};
    double native_send_frame_ms_{0.0};
    double native_backpressure_ms_{0.0};
    double native_flush_ms_{0.0};
    double native_receive_packet_ms_{0.0};
    double native_mux_write_ms_{0.0};
    double native_trailer_ms_{0.0};
    EncoderFrameTelemetry last_frame_telemetry_{};

    /// Drain all pending packets from the encoder after avcodec_send_frame.
    bool drain_packets();
    bool drain_ready_cuda_frames(bool wait_for_one);
    void abort_open() noexcept;

    // ── Single-entry YUV conversion cache ──
    // When consecutive frames have the same digest (static scenes), skip the
    // expensive RGBA→YUV conversion entirely and reuse the AVFrame planes
    // from the previous conversion.  No memcpy needed — the data is already there.
    uint64_t last_converted_digest_{0};
    int      last_converted_width_{0};
    int      last_converted_height_{0};
    int      last_converted_pix_fmt_{-1};
    bool     last_converted_apply_gamma_{false};
    int      last_converted_color_matrix_{-1};
    uint64_t cache_hits_{0};
    uint64_t cache_misses_{0};
};

} // namespace chronon3d::cli
