#pragma once

#include "ffmpeg_pipe_encoder.hpp"
#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/media/video/direct_yuv_frame.hpp>
#include "cuda_context_guard.hpp"
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
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

class NativeAvEncoder : public IVideoEncoder {
public:
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
    bool write_direct_yuv(const media::video::DirectYuvFrame& frame) override;
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

    void shutdown_noexcept() noexcept;

    [[nodiscard]] uint64_t frames_written() const override { return frames_written_; }
    [[nodiscard]] EncoderFrameTelemetry last_frame_telemetry() const override { return last_frame_telemetry_; }

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

    [[nodiscard]] bool pool_reset_safe() const noexcept {
        return !open_complete_ && !codec_ && !mux_ && !frame_ && !packet_;
    }

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

    std::unique_ptr<chronon3d::media::MuxSession> mux_;
    AVCodecContext*  codec_{nullptr};
    AVFrame*         frame_{nullptr};
    AVPacket*        packet_{nullptr};
    AVFormatContext* audio_input_{nullptr};
    int audio_input_stream_{-1};

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    static constexpr std::size_t kCudaEncodeRingSlots = 6;
#ifdef CHRONON3D_ENABLE_VULKAN
    std::unordered_map<std::uint64_t,
        std::unique_ptr<backends::vulkan::CudaNv12SurfaceCompositor>>
        cuda_nv12_compositors_;
#endif
    std::unique_ptr<media::CudaDirectNv12Compositor>
        direct_yuv_compositor_;
    std::deque<AVFrame*> reusable_cuda_frames_;
    struct PendingCudaFrame {
        AVFrame* frame{nullptr};
        cuda::OwnedCudaEvent ready{};
        runtime::RenderSurfaceHandle surface{runtime::kInvalidRenderSurfaceHandle};
        std::shared_ptr<const media::video::DirectYuvTemplate> program;
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
    std::shared_ptr<media::VideoDeviceRuntime> device_runtime_;
    bool gpu_nvenc_{false};
    bool open_complete_{false};

    double native_convert_ms_{0.0};
    double native_send_frame_ms_{0.0};
    double native_backpressure_ms_{0.0};
    double native_flush_ms_{0.0};
    double native_receive_packet_ms_{0.0};
    double native_mux_write_ms_{0.0};
    double native_trailer_ms_{0.0};
    EncoderFrameTelemetry last_frame_telemetry_{};

    bool drain_packets();
    bool mux_source_audio();
    bool drain_ready_cuda_frames(bool wait_for_one);
    void abort_open() noexcept;
    bool write_native_surface_impl(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination,
        bool surface_already_prepared);

    uint64_t last_converted_digest_{0};
    int      last_converted_width_{0};
    int      last_converted_height_{0};
    int      last_converted_pix_fmt_{-1};
    bool     last_converted_apply_gamma_{false};
    int      last_converted_color_matrix_{-1};
    uint64_t cache_hits_{0};
    uint64_t cache_misses_{0};
    mutable std::recursive_mutex mutex_;
};

} // namespace chronon3d::cli
