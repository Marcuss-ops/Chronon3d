#pragma once

#include "../../commands.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include "../../utils/common/cli_mappers.hpp"
#include "../../utils/video/frame_chunks.hpp"
#include "../../utils/video/ffmpeg_pipe_encoder.hpp"
#include "../../utils/telemetry/telemetry_run.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <string>
#include <memory>

namespace chronon3d::cli {

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
    size_t warmup_framebuffers{8};
    bool   warmup_dummy_frame{false};

    // Graceful cancellation (optional — set by command_video SIGINT handler)
    chronon3d::CancellationToken* cancellation_token{nullptr};
};

int render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts);

// Factory: creates the appropriate encoder based on opts.encoder_backend
std::unique_ptr<IVideoEncoder> create_video_encoder(const FfmpegExportOptions& opts);

int render_and_encode_ffmpeg_chunked(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts);

} // namespace chronon3d::cli
