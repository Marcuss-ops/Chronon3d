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
#include "sink_options.hpp"
#include "output_options.hpp"
#include "encoder_options.hpp"
#include "pipe_options.hpp"
#include "warmup_options.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <string>
#include <memory>

namespace chronon3d::cli {

// VideoSinkType is now defined in sink_options.hpp.
// Use parse_video_sink_type() to parse CLI strings to VideoSinkType.
// Use chronon3d::cli::to_string() to convert VideoSinkType to string.

bool ffmpeg_in_path();
PipePixelFormat parse_pipe_pixfmt(const std::string& fmt);
color::ColorSpace parse_color_output(const std::string& cs);
std::string resolve_cli_ffmpeg_codec(const std::string& codec, const std::string& hw_encoder);
std::string resolve_cli_ffmpeg_output_pix_fmt(const std::string& codec);

struct FfmpegExportOptions {
    OutputOptions         output;
    EncoderOptions        encoder;
    PipeOptions           pipe;
    RenderWarmupOptions   warmup;
    SinkOptions           sink;

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
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget);

/// Canonical video-pipeline evaluate: threads the renderer's FontEngine
/// into the composition evaluation so text shapes materialize correctly.
/// All video exporters MUST use this instead of calling comp.evaluate()
/// directly — without the engine, materialize_text_run_shape logs
/// "no FontEngine available" and returns nullptr, causing blank text.
///
/// Non reintroduce shared_font_engine() or a global singleton:
/// the project removed them intentionally.
///
/// SampleTime overload — canonical.  Decomposes into integral frame +
/// sub-frame fraction for the engine-aware evaluate(Frame, f32, FontEngine*)
/// path.  Callers with a Frame can use the convenience overload below.
inline Scene evaluate_video_scene(
    const Composition& comp,
    SampleTime time,
    SoftwareRenderer& renderer)
{
    return comp.evaluate(time.integral_frame(),
                         static_cast<f32>(time.fraction()),
                         &renderer.font_engine());
}

/// Convenience overload for callers that only have a discrete Frame
/// (e.g. integer frame loops in chunked/pipe exporters).  Converts to
/// SampleTime at the composition's native frame rate.
inline Scene evaluate_video_scene(
    const Composition& comp,
    Frame frame,
    SoftwareRenderer& renderer)
{
    return evaluate_video_scene(comp,
                                SampleTime::from_frame_int(frame, comp.frame_rate()),
                                renderer);
}

} // namespace chronon3d::cli
