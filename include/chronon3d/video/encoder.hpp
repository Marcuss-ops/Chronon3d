#pragma once

// =============================================================================
// include/chronon3d/video/encoder.hpp — Abstract video encoder interface
//
// Part of chronon3d_backend_video.  Defines the IVideoEncoder contract that
// all encoder implementations (pipe subprocess, native libav, null sinks)
// must satisfy.  CLI-level orchestration lives in chronon3d_cli_video and
// depends on this header.
// =============================================================================

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <thread>
#include <memory>
#include <cstdint>

#include <tbb/task_arena.h>

namespace chronon3d::video {

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

/// Pixel format used for the pipe input / internal encoder buffer.
enum class PipePixelFormat {
    RGBA,
    YUV420P,
    NV12
};

/// Options for configuring a video encoder (pipe subprocess or native).
///
/// Shared by FfmpegPipeEncoder, NativeAvEncoder, and all sink encoders.
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
    std::string tune;                          // x264 tune (empty = default "zerolatency")
    std::string pipe_writer{"classic"};         // "io_uring" to opt-in to experimental async I/O
};

/// Build the ffmpeg CLI command line for a raw-video pipe.
/// Declared here because it is called by FfmpegPipeEncoder::open().
[[nodiscard]] std::string build_ffmpeg_raw_pipe_command(const FfmpegPipeOptions& options);

/// Resolve the colour-matrix id used for cache-key computation.
[[nodiscard]] int encode_color_matrix_id(const FfmpegPipeOptions& options);

// ── Abstract video encoder interface ────────────────────────────────────────
// Implemented by both FfmpegPipeEncoder (external ffmpeg subprocess via pipe)
// and NativeAvEncoder (in-process libavcodec/libavformat).
struct IVideoEncoder {
    virtual ~IVideoEncoder() = default;
    virtual bool open(const FfmpegPipeOptions& options) = 0;
    virtual bool write_frame(const Framebuffer& fb) = 0;
    virtual bool write_frame_async(const Framebuffer& fb, std::shared_ptr<Framebuffer> owner) {
        return write_frame(fb);
    }
    virtual void set_counters(chronon3d::RenderCounters* counters) { (void)counters; }
    virtual bool close() = 0;
    [[nodiscard]] virtual uint64_t frames_written() const = 0;
    [[nodiscard]] virtual EncoderFrameTelemetry last_frame_telemetry() const { return {}; }

    // ── Native encoder telemetry accessors ──
    // Return 0.0 for pipe backend (no native encoding phases).
    [[nodiscard]] virtual double native_convert_ms()     const { return 0.0; }
    [[nodiscard]] virtual double native_send_frame_ms()  const { return 0.0; }
    [[nodiscard]] virtual double native_receive_packet_ms() const { return 0.0; }
    [[nodiscard]] virtual double native_mux_write_ms()   const { return 0.0; }
    [[nodiscard]] virtual double native_trailer_ms()     const { return 0.0; }
};

} // namespace chronon3d::video
