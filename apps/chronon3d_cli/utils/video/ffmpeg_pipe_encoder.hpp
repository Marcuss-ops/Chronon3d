#pragma once

// =============================================================================
// ffmpeg_pipe_encoder.hpp — Abstract IVideoEncoder interface  // drift-class: historical (include/chronon3d/video/encoder.hpp retired; interface kept in CLI utils)
// (kept in CLI utils for incremental migration; will move to shared lib)
// =============================================================================

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/render_surface_handle.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace chronon3d::cli {

struct DirectYuvFrame;

/// Per-frame telemetry captured by the encoder during write_frame().
struct EncoderFrameTelemetry {
    double conversion_copy_ms{0.0};
    // Conversion sub-phases: float→RGBA8 quantization and RGBA→YUV color
    // space.  scale/cpu_copy/gpu_readback/encoder_buffer_copy are not
    // separable on the software path and stay 0 (emitted as JSON null).
    double pixel_format_convert_ms{0.0};
    double color_space_convert_ms{0.0};
    double encoder_ms{0.0};
    double pipe_write_ms{0.0};
    // Encoder back-pressure wait (EAGAIN drain+retry) separated from the
    // pure submit CPU time.  Zero on the pipe path (synchronous sink submit
    // folds back-pressure into the submit wall time).
    double backpressure_wait_ms{0.0};
    // Pipe write decomposed into CPU copy (::write() syscall) vs poll()
    // back-pressure wait (blocked on FFmpeg draining stdin).  Native-only
    // path leaves both at 0.0.
    double pipe_write_cpu_ms{0.0};
    double pipe_backpressure_wait_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
    uint64_t conversion_bytes_written{0};
    uint64_t encoder_staging_copy_bytes{0};
    uint64_t encoder_slots_allocated{0};
    uint64_t encoder_slot_reuses{0};
    double frame_submit_ms{0.0};
};

// ── Abstract video encoder interface ────────────────────────────────────────
// Implemented by VideoSinkEncoderAdapter (pipe), NativeAvEncoder (native),
// NullRenderEncoder, and NullConvertEncoder.
struct IVideoEncoder {
    virtual ~IVideoEncoder() = default;
    virtual bool open(const struct FfmpegPipeOptions& options) = 0;
    virtual bool write_frame(const Framebuffer& fb) = 0;
    virtual bool write_direct_yuv(const DirectYuvFrame& frame) {
        (void)frame;
        return false;
    }
    virtual bool write_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination) {
        (void)backend; (void)source; (void)destination;
        return false;
    }
    virtual bool write_prepared_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination) {
        return write_native_surface(backend, source, destination);
    }
    /// Blocks until the encoder has finished all asynchronous CUDA reads of
    /// `destination`. The writer must call this before releasing the
    /// interop-ring slot, otherwise Vulkan can recycle the image while CUDA
    /// still owns it.
    virtual bool finish_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle destination) {
        (void)backend; (void)destination;
        return true;
    }
    /// Non-blocking completion probe for asynchronous surface retirement.
    virtual bool poll_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle destination) {
        (void)backend; (void)destination;
        return true;
    }
    virtual bool write_frame_async(const Framebuffer& fb, std::shared_ptr<Framebuffer> owner) {
        return write_frame(fb);
    }
    virtual void set_counters(chronon3d::RenderCounters* counters) { (void)counters; }

    // CUDA context owning hardware frames. Native encoders expose the
    // FFmpeg CUDA context after open(); GPU frame importers must use this
    // exact context rather than sampling whichever context happens to be
    // current during pipeline construction.
    [[nodiscard]] virtual void* cuda_context() const { return nullptr; }
    virtual bool close() = 0;
    [[nodiscard]] virtual uint64_t frames_written() const = 0;
    [[nodiscard]] virtual EncoderFrameTelemetry last_frame_telemetry() const { return {}; }

    // ── Native encoder telemetry accessors ──
    [[nodiscard]] virtual double native_convert_ms()     const { return 0.0; }
    [[nodiscard]] virtual double native_send_frame_ms()  const { return 0.0; }
    [[nodiscard]] virtual double native_backpressure_ms() const { return 0.0; }
    [[nodiscard]] virtual std::uint64_t native_cuda_pending_peak() const { return 0; }
    [[nodiscard]] virtual std::uint64_t native_cuda_backpressure_wait_count() const { return 0; }
    [[nodiscard]] virtual double native_flush_ms()       const { return 0.0; }
    [[nodiscard]] virtual double native_receive_packet_ms() const { return 0.0; }
    [[nodiscard]] virtual double native_mux_write_ms()   const { return 0.0; }
    [[nodiscard]] virtual double native_trailer_ms()     const { return 0.0; }
    [[nodiscard]] virtual double encoder_hwframe_get_buffer_ms() const { return 0.0; }
    [[nodiscard]] virtual double encoder_surface_acquire_ms() const { return 0.0; }
    [[nodiscard]] virtual double encoder_nvenc_submit_ms() const { return 0.0; }
    [[nodiscard]] virtual double encoder_queue_backpressure_wait_ms() const { return 0.0; }
    [[nodiscard]] virtual double encoder_packet_drain_ms() const { return 0.0; }
    [[nodiscard]] virtual double direct_yuv_cuda_launch_ms() const { return 0.0; }
    [[nodiscard]] virtual double direct_yuv_cuda_wait_ms() const { return 0.0; }
    [[nodiscard]] virtual double open_hw_ctx_ms() const { return 0.0; }
    [[nodiscard]] virtual double cuda_compositor_warmup_ms() const { return 0.0; }
    [[nodiscard]] virtual double open_nvenc_ms() const { return 0.0; }
    [[nodiscard]] virtual double open_mux_header_ms() const { return 0.0; }

    // ── Pipe encoder telemetry accessors ──
    [[nodiscard]] virtual double total_write_blocked_ms() const { return 0.0; }
    [[nodiscard]] virtual int    ffmpeg_pid() const { return -1; }
};

/// Pixel format used for the pipe input / internal encoder buffer.
enum class PipePixelFormat {
    RGBA,
    YUV420P,
    NV12
};

/// Options for configuring a video encoder.
struct FfmpegPipeOptions {
    int width{0};
    int height{0};
    int fps{30};
    int fps_num{30};
    int fps_den{1};
    std::string rate_control_mode{"crf"};
    int crf{18};
    int qp{-1};
    std::int64_t bitrate{0};
    std::string preset{"medium"};
    std::string codec{"libx264"};
    std::string hardware_encoder{"none"};
    std::string output_path;
    PipePixelFormat input_format{PipePixelFormat::RGBA};
    std::string output_pix_fmt{"yuv420p"};
    bool verbose{false};
    color::OutputTransformOptions color_transform{};
    std::string tune;
    std::string pipe_writer{"classic"};
    int encode_threads{0};  ///< 0 = use encoder default (legacy behaviour)
    // Direct-YUV owns its CUDA-only compositor and must not warm the
    // Vulkan/FullGraph compositor while opening the encoder.
    bool direct_yuv_mode{false};
    // Optional source audio for the native A/V mux path. The source is
    // opened before the mux header so its codec parameters are declared in
    // the same MuxOpenConfig as video.
    std::string audio_source_path;
    double audio_start_seconds{0.0};
    double audio_end_seconds{0.0};

    [[nodiscard]] int canonical_fps_num() const noexcept {
        return (fps_num == 30 && fps_den == 1 && fps != 30) ? fps : fps_num;
    }
    [[nodiscard]] int canonical_fps_den() const noexcept {
        return (fps_num == 30 && fps_den == 1 && fps != 30) ? 1 : fps_den;
    }
};

} // namespace chronon3d::cli
