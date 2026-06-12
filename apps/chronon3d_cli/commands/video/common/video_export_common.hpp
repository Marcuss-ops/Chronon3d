#pragma once

#include "../../../commands.hpp"
#include "../../../utils/job/cli_render_utils.hpp"
#include "../../../utils/common/cli_mappers.hpp"
#include "../../../utils/video/frame_chunks.hpp"
#include "../../../utils/video/ffmpeg_pipe_encoder.hpp"
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include "../../../utils/video/native_av_encoder.hpp"
#endif
#include "../../../utils/telemetry/telemetry_run.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <string>
#include <memory>

namespace chronon3d::cli {

// ── Video sink modes for pipeline isolation ────────────────────────────────
enum class VideoSinkMode {
    Ffmpeg,       // full pipeline: render → convert → FFmpeg pipe (current default)
    NullRender,   // render only — skip conversion and output entirely
    NullConvert,  // render + convert — skip FFmpeg pipe write
    Raw,          // render + convert + write raw bytes to file (no FFmpeg)
};

inline VideoSinkMode parse_video_sink_mode(const std::string& value) {
    if (value == "ffmpeg")       return VideoSinkMode::Ffmpeg;
    if (value == "null-render")  return VideoSinkMode::NullRender;
    if (value == "null-convert") return VideoSinkMode::NullConvert;
    if (value == "raw")          return VideoSinkMode::Raw;
    throw std::runtime_error("Invalid --video-sink: " + value);
}

inline const char* video_sink_mode_name(VideoSinkMode mode) {
    switch (mode) {
        case VideoSinkMode::Ffmpeg:       return "ffmpeg";
        case VideoSinkMode::NullRender:  return "null-render";
        case VideoSinkMode::NullConvert: return "null-convert";
        case VideoSinkMode::Raw:         return "raw";
    }
    return "unknown";
}

bool ffmpeg_in_path();
PipePixelFormat parse_pipe_pixfmt(const std::string& fmt);
color::ColorSpace parse_color_output(const std::string& cs);
std::string resolve_cli_ffmpeg_codec(const std::string& codec, const std::string& hw_encoder);
std::string resolve_cli_ffmpeg_output_pix_fmt(const std::string& codec);

struct FfmpegExportOptions {
    std::string output;
    std::string frames_dir_name;
    int fps;
    int crf;
    std::string codec;
    std::string hardware_encoder;
    std::string encode_preset;
    std::string tune;
    bool keep_frames;
    int chunks;
    std::string ffmpeg_mode{"pipe"};
    bool ffmpeg_verbose{false};
    std::string pipe_pixfmt{"rgba"};
    std::string color_output{"srgb"};
    std::string pipe_writer{"classic"};
    std::string encoder_backend{"native"};

    // Renderer warmup
    bool   warmup_renderer{false};
    size_t warmup_framebuffers{2};
    bool   warmup_dummy_frame{false};

    // Video sink mode for pipeline isolation measurement
    VideoSinkMode sink_mode{VideoSinkMode::Ffmpeg};
    std::string video_sink{"ffmpeg"};

    // Graceful cancellation (optional — set by command_video SIGINT handler)
    chronon3d::CancellationToken* cancellation_token{nullptr};
};

// render_and_encode_ffmpeg_pipe() is declared in pipe_export_session.hpp
// and returns PipeExportResult (boundary model with all status/timing data).

/// Result boundary model for chunked export (PNG frames → ffmpeg encode).
struct ChunkedExportResult {
    int return_code{1};
    bool success{false};
    bool chunk_failed{false};
    bool encode_failed{false};
    int frames_written{0};
    int frames_total{0};
    double wall_time_ms{0.0};
    double render_ms{0.0};
    double encode_ms{0.0};
};

// Factory: creates the appropriate encoder based on opts.encoder_backend
std::unique_ptr<IVideoEncoder> create_video_encoder(const FfmpegExportOptions& opts);

[[nodiscard]] ChunkedExportResult render_and_encode_ffmpeg_chunked(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts);

} // namespace chronon3d::cli
