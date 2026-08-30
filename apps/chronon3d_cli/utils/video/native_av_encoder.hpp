#pragma once

#include "ffmpeg_pipe_encoder.hpp"
#include <chronon3d/media/video/packet_assembler.hpp>
#include "direct_yuv_frame.hpp"
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <chronon3d/media/video/cuda_direct_nv12_compositor.hpp>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>
#endif
#endif


extern "C" {
#include <libavcodec/avcodec.h>
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
    /// Borrows the persistent GPU device runtime (primary CUDA context +
    /// FFmpeg hwdevice). NVENC open() fails closed without it.
    explicit NativeAvEncoder(
        std::shared_ptr<media::VideoDeviceRuntime> device_runtime = nullptr);
    ~NativeAvEncoder() override;

    NativeAvEncoder(const NativeAvEncoder&) = delete;
    NativeAvEncoder& operator=(const NativeAvEncoder&) = delete;
    NativeAvEncoder(NativeAvEncoder&&) = delete;
    NativeAvEncoder& operator=(NativeAvEncoder&&) = delete;

    bool open(const FfmpegPipeOptions& options) override;
    void set_counters(chronon3d::RenderCounters* counters) override {
        counters_ = counters;
    }
    [[nodiscard]] void* cuda_context() const override {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        return cuda_context_;
#else
        return nullptr;
#endif
    }
    bool write_frame(const Framebuffer& fb) override;
    bool write_direct_yuv(const DirectYuvFrame& frame) override;
    bool write_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination) override;
    bool write_prepared_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination) override;
    bool finish_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle destination) override;
    bool poll_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle destination) override;
    bool close() override;

    /// Safe, idempotent cleanup used by close(), the destructor, and failed
    /// partial-open paths. It never throws and leaves all owned FFmpeg/CUDA
    /// handles null.
    void shutdown_noexcept() noexcept;

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
    [[nodiscard]] double encoder_hwframe_get_buffer_ms() const override { return encoder_hwframe_get_buffer_ms_; }
    [[nodiscard]] double encoder_surface_acquire_ms() const override { return encoder_surface_acquire_ms_; }
    [[nodiscard]] double encoder_nvenc_submit_ms() const override { return encoder_nvenc_submit_ms_; }
    [[nodiscard]] double encoder_queue_backpressure_wait_ms() const override { return encoder_queue_backpressure_wait_ms_; }
    [[nodiscard]] double encoder_packet_drain_ms() const override { return encoder_packet_drain_ms_; }
    [[nodiscard]] double direct_yuv_cuda_launch_ms() const override { return direct_yuv_cuda_launch_ms_; }
    [[nodiscard]] double direct_yuv_cuda_wait_ms() const override { return direct_yuv_cuda_wait_ms_; }
    [[nodiscard]] double open_hw_ctx_ms() const override { return open_hw_ctx_ms_; }
    [[nodiscard]] double cuda_compositor_warmup_ms() const override { return cuda_compositor_warmup_ms_; }
    [[nodiscard]] double open_nvenc_ms() const override { return open_nvenc_ms_; }
    [[nodiscard]] double open_mux_header_ms() const override { return mux_ ? mux_->open_header_ms() : 0.0; }

private:
    chronon3d::RenderCounters* counters_{nullptr};
    FfmpegPipeOptions options_{};
    uint64_t frames_written_{0};
    double open_hw_ctx_ms_{0.0};
    double cuda_compositor_warmup_ms_{0.0};
    double open_nvenc_ms_{0.0};
    double encoder_hwframe_get_buffer_ms_{0.0};
    double encoder_surface_acquire_ms_{0.0};
    double encoder_nvenc_submit_ms_{0.0};
    double encoder_queue_backpressure_wait_ms_{0.0};
    double encoder_packet_drain_ms_{0.0};
    double direct_yuv_cuda_launch_ms_{0.0};
    double direct_yuv_cuda_wait_ms_{0.0};

    // FFmpeg objects
    std::unique_ptr<chronon3d::media::MuxSession> mux_;
    AVCodecContext*  codec_{nullptr};
    AVFrame*         frame_{nullptr};
    AVPacket*        packet_{nullptr};

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    static constexpr std::size_t kCudaEncodeRingSlots = 6;
#ifdef CHRONON3D_ENABLE_VULKAN
    std::unordered_map<std::uint64_t,
        std::unique_ptr<backends::vulkan::CudaNv12SurfaceCompositor>>
        cuda_nv12_compositors_;
#endif
    std::unique_ptr<media::CudaDirectNv12Compositor>
        direct_yuv_compositor_;
    // AVFrame wrappers are recycled across the bounded CUDA/NVENC ring. The
    // hw-frame pool remains owned by FFmpeg; only the wrapper lifetime is
    // reused here, avoiding an av_frame_alloc/free pair per encoded frame.
    std::deque<AVFrame*> reusable_cuda_frames_;
    struct PendingCudaFrame {
        AVFrame* frame{nullptr};
        CUevent ready{nullptr};
        runtime::RenderSurfaceHandle surface{runtime::kInvalidRenderSurfaceHandle};
        // Keep the Direct-YUV overlay program (batch, resources and the
        // device-resident watermark image) alive until NVENC has consumed the
        // encoded output.  Typed replacement for the old shared_ptr<void>.
        std::shared_ptr<const DirectYuvTemplate> program;
        // Keep the NVDEC surface referenced until the direct-YUV kernel has
        // completed.  Releasing it when the queue package is destroyed can
        // make NVDEC block while trying to recycle its surface pool.
        media::HwFrameRef source_owner;
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
    // Persistent device runtime borrowed from the job's registry. The
    // primary CUDA context + FFmpeg hwdevice are owned there, not here.
    std::shared_ptr<media::VideoDeviceRuntime> device_runtime_;
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
    bool write_native_surface_impl(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination,
        bool surface_already_prepared);

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
