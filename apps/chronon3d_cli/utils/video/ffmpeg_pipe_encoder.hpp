#pragma once

// =============================================================================
// ffmpeg_pipe_encoder.hpp — Abstract IVideoEncoder interface
// =============================================================================

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/render_surface_handle.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace chronon3d::media::video {
struct DirectYuvFrame;
}

namespace chronon3d::cli {

struct EncoderFrameTelemetry {
    double conversion_copy_ms{0.0};
    double pixel_format_convert_ms{0.0};
    double color_space_convert_ms{0.0};
    double encoder_ms{0.0};
    double pipe_write_ms{0.0};
    double backpressure_wait_ms{0.0};
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

struct IVideoEncoder {
    virtual ~IVideoEncoder() = default;
    virtual bool open(const struct FfmpegPipeOptions& options) = 0;
    virtual bool write_frame(const Framebuffer& fb) = 0;
    virtual bool write_direct_yuv(const media::video::DirectYuvFrame& frame) {
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
    virtual bool finish_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle destination) {
        (void)backend; (void)destination;
        return true;
    }
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

    [[nodiscard]] virtual void* cuda_context() const { return nullptr; }
    virtual bool close() = 0;
    [[nodiscard]] virtual uint64_t frames_written() const = 0;
    [[nodiscard]] virtual EncoderFrameTelemetry last_frame_telemetry() const { return {}; }

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

    // Encoder settings actually applied at open() time (post-default
    // resolution). Empty/0 means "not applicable" (e.g. software encoder or
    // default virtual). Surfaced so certification can verify in the timing
    // sidecar which NVENC preset/RC/async_depth really ran.
    [[nodiscard]] virtual std::string applied_encoder_preset() const { return {}; }
    [[nodiscard]] virtual std::string applied_encoder_rate_control() const { return {}; }
    [[nodiscard]] virtual int applied_encoder_async_depth() const { return 0; }

    // Encoder settings the caller requested ("engine-default" when the value
    // was never explicitly requested). Together with the applied_* accessors
    // above this lets telemetry reveal requested vs resolved configuration
    // instead of pretending an engine default was an explicit user choice.
    [[nodiscard]] virtual std::string requested_encoder_rate_control() const { return {}; }
    [[nodiscard]] virtual std::string requested_encoder_preset() const { return {}; }

    [[nodiscard]] virtual double total_write_blocked_ms() const { return 0.0; }
    [[nodiscard]] virtual int    ffmpeg_pid() const { return -1; }
};

enum class PipePixelFormat {
    RGBA,
    YUV420P,
    NV12
};

struct FfmpegPipeOptions {
    int width{0};
    int height{0};
    int fps{30};
    int fps_num{30};
    int fps_den{1};

    // ── Encoder configuration (rate control / quality) ────────────────────
    //
    // The `*_explicit` markers distinguish a USER REQUEST from a CHRONON
    // engine placeholder default. A default must never be indistinguishable
    // from an explicit request: the encoder-configuration resolver (see
    // encoder_config_resolution.hpp) rejects unsupported combinations only
    // when they were explicitly requested, and applies the deterministic
    // engine default otherwise. All plumbing (CLI render, render-plan, IPC,
    // video jobs) must set these markers from the original source of the
    // value; never leave an explicit value unmarked.
    std::string rate_control_mode{"crf"};
    bool rate_control_mode_explicit{false};
    int crf{18};
    bool crf_explicit{false};
    int qp{-1};
    bool qp_explicit{false};
    std::int64_t bitrate{0};
    bool bitrate_explicit{false};
    int async_depth{0};
    std::string preset{"medium"};
    bool preset_explicit{false};
    std::string codec{"libx264"};
    std::string hardware_encoder{"none"};
    std::string output_path;
    PipePixelFormat input_format{PipePixelFormat::RGBA};
    std::string output_pix_fmt{"yuv420p"};
    bool verbose{false};
    color::OutputTransformOptions color_transform{};
    std::string tune;
    bool tune_explicit{false};
    std::string pipe_writer{"classic"};
    int encode_threads{0};
    bool direct_yuv_mode{false};
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
