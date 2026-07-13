#pragma once

// =============================================================================
// include/chronon3d/video/encoder.hpp — Abstract IVideoEncoder interface  // drift-allow: stale-ref
// (kept in CLI utils for incremental migration; will move to shared lib)
// =============================================================================

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <memory>
#include <string>

namespace chronon3d::cli {

/// Per-frame telemetry captured by the encoder during write_frame().
struct EncoderFrameTelemetry {
    double conversion_copy_ms{0.0};
    double encoder_ms{0.0};
    double pipe_write_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
};

// ── Abstract video encoder interface ────────────────────────────────────────
// Implemented by VideoSinkEncoderAdapter (pipe), NativeAvEncoder (native),
// NullRenderEncoder, and NullConvertEncoder.
struct IVideoEncoder {
    virtual ~IVideoEncoder() = default;
    virtual bool open(const struct FfmpegPipeOptions& options) = 0;
    virtual bool write_frame(const Framebuffer& fb) = 0;
    virtual bool write_frame_async(const Framebuffer& fb, std::shared_ptr<Framebuffer> owner) {
        return write_frame(fb);
    }
    virtual void set_counters(chronon3d::RenderCounters* counters) { (void)counters; }
    virtual bool close() = 0;
    [[nodiscard]] virtual uint64_t frames_written() const = 0;
    [[nodiscard]] virtual EncoderFrameTelemetry last_frame_telemetry() const { return {}; }

    // ── Native encoder telemetry accessors ──
    [[nodiscard]] virtual double native_convert_ms()     const { return 0.0; }
    [[nodiscard]] virtual double native_send_frame_ms()  const { return 0.0; }
    [[nodiscard]] virtual double native_receive_packet_ms() const { return 0.0; }
    [[nodiscard]] virtual double native_mux_write_ms()   const { return 0.0; }
    [[nodiscard]] virtual double native_trailer_ms()     const { return 0.0; }

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
    int crf{18};
    std::string preset{"medium"};
    std::string codec{"libx264"};
    std::string output_path;
    PipePixelFormat input_format{PipePixelFormat::RGBA};
    std::string output_pix_fmt{"yuv420p"};
    bool verbose{false};
    color::OutputTransformOptions color_transform{};
    std::string tune;
    std::string pipe_writer{"classic"};
    int encode_threads{0};  ///< 0 = use encoder default (legacy behaviour)
};

} // namespace chronon3d::cli
