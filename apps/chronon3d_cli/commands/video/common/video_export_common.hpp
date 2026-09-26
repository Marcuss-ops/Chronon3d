#pragma once
#ifndef CHRONON3D_CLI_VIDEO_EXPORT_COMMON_HPP
#define CHRONON3D_CLI_VIDEO_EXPORT_COMMON_HPP

#include "../../../commands.hpp"
#include "../../../utils/job/cli_render_utils.hpp"
#include "../../../utils/common/cli_mappers.hpp"
#include "../../../utils/video/ffmpeg_pipe_encoder.hpp"  // PipePixelFormat + pipe_pixel_format_name
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include "../../../utils/video/native_av_encoder.hpp"
#endif
#include "../../../utils/telemetry/telemetry_run.hpp"
#include "sink_options.hpp"
#include "output_options.hpp"
#include "encoder_options.hpp"
#include "pipe_options.hpp"
#include "warmup_options.hpp"
#include "../../../utils/video/variant_batch.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <string>
#include <string_view>
#include <filesystem>
#include <optional>
#include <memory>
#include <cstdint>

namespace chronon3d::cli {

// VideoSinkType is now defined in sink_options.hpp.
// Use parse_video_sink_type() to parse CLI strings to VideoSinkType.
// Use chronon3d::cli::to_string() to convert VideoSinkType to string.

/// Video export encoding is resolved by the in-process media sink.  The
/// historical transport field remains part of the wire contract for
/// compatibility, but export validation must not probe or execute an external
/// `ffmpeg` binary.

/// Canonical parse of a pipe pixel format string.
///
/// Delegates to the ONE authority (`runtime::try_parse_pixel_format` in
/// <chronon3d/runtime/frame_format.hpp>): a name outside the closed set yields
/// `std::nullopt` and the caller must fail loud. The previous
/// `parse_pipe_pixfmt()` mapped every unknown value onto RGBA, so a typo or a
/// trailing space silently changed the exported pixel format.
[[nodiscard]] std::optional<PipePixelFormat> try_parse_pipe_pixfmt(
    std::string_view fmt) noexcept;
color::ColorSpace parse_color_output(const std::string& cs);
std::string resolve_cli_ffmpeg_codec(const std::string& codec, const std::string& hw_encoder);
std::string resolve_cli_ffmpeg_output_pix_fmt(const std::string& codec);

// The borrowed-container target (BorrowedMuxTarget) lives with the pipe
// options that carry it — utils/video/ffmpeg_pipe_encoder.hpp — so the encoder
// layer can describe and consume it without depending on the CLI command
// header.

struct FfmpegExportOptions {
    OutputOptions         output;
    EncoderOptions        encoder;
    PipeOptions           pipe;
    RenderWarmupOptions   warmup;
    SinkOptions           sink;
    // Explicit per-render asset mount; never inferred from the process CWD.
    std::optional<std::filesystem::path> assets_root;
    // Authoritative source-time intervals used by the GOP preflight. An
    // empty list retains the legacy whole-export content-change behavior.
    std::vector<chronon3d::media::ChangedInterval> changed_intervals;

    // Rendering backend preference carried from the canonical RenderJob's
    // Config (--backend auto|software|vulkan).  Auto resolves to Software on
    // the video pipe path; GPU strictly resolves the Vulkan backend.
    chronon3d::graph::BackendPreference backend_preference{
        chronon3d::graph::BackendPreference::Auto};
    chronon3d::GpuHotPathMode gpu_hot_path_mode{
        chronon3d::GpuHotPathMode::Auto};
    // Set only by video_job_execute after the canonical resolver decision.
    // Lower-level exporters must not infer or resolve the execution path.
    enum class ResolvedExecutionPath : std::uint8_t {
        DirectYuv,
        FullGraph,
    };
    ResolvedExecutionPath resolved_execution_path{ResolvedExecutionPath::FullGraph};
    // The resolver's authoritative orthogonal FINAL plan. It is optional only
    // for legacy direct callers that predate the resolver boundary.
    std::optional<chronon3d::media::VideoExecutionPlan> resolved_execution_plan;
    // Compiler-owned GOP schedule. The render/mux executor may consume this
    // verbatim; it must never reconstruct copy/reencode decisions from the
    // scene or from the source a second time.
    // There is deliberately NO `resolved_gop_plan` field here. The GOP axis of
    // the decision stays in `media::VideoExecutionDecision::gop_plan`, where it
    // is asserted by tests; the CLI executor never consumed a copy of it — the
    // hybrid lane derives its schedule with `build_gop_plan_from_analysis` from
    // the analysis it just performed, which is the single producer of
    // `GopWorkUnit`s. A second copy that nothing reads is how a declared plan
    // and an executed plan drift apart (audit: dead state).
    // Structured resolver reason for the final plan (for example
    // "hybrid_gop_boundary_reencode"). Receipt surface: surfaced in logs
    // today and available for timing-sidecar/telemetry persistence.
    std::string resolved_execution_reason;
    // Resolver-owned downgrade receipt. Zero means that the final plan was
    // selected directly; it never means that an executor may fallback.
    std::uint32_t resolved_execution_downgrade_count{0};
    // Resolver validity for the final plan. A false value is fail-closed and
    // means the requested requirements cannot be executed on this host.
    bool resolved_execution_valid{true};
    // Graceful cancellation (optional — set by command_video SIGINT handler)
    chronon3d::CancellationToken* cancellation_token{nullptr};

    // Optional compressed source for packet-level GOP analysis.
    std::string gop_source;
    std::string audio_source_path;
    /// Requested OUTPUT audio sample rate (Hz) for the muxed master audio.
    /// 0 keeps the source rate (copy semantics); a positive rate that differs
    /// from the source makes the native mux resample the audio to that rate.
    int audio_target_sample_rate{0};
    bool gop_copy_only{false};

    // P0-5E: set only by the direct-mux hybrid executor for the child render of
    // a re-encoded work unit. Unset (the normal case) means the encoder owns
    // its own container exactly as before.
    std::optional<BorrowedMuxTarget> borrowed_mux{};

    // Optional SIMO outputs. An empty list preserves the single-output path;
    // populated variants share the render master and fan out at the encoder
    // boundary according to VariantBatchPlan.
    std::vector<OutputVariant> variants;
};

// render_and_encode_native() is declared in pipe_export_pipeline.hpp
// and returns PipeExportResult (boundary model with all status/timing data).

/// Context handed to the encoder factory. The encoder does NOT discover or
/// retain CUDA itself anymore: the device runtime already owns the primary
/// CUDA context + FFmpeg hwdevice for the selected device.
struct VideoEncoderCreateContext {
    std::shared_ptr<media::VideoDeviceRuntime> device_runtime;
};

// Factory: creates the appropriate encoder based on opts.encoder_backend
std::unique_ptr<IVideoEncoder> create_video_encoder(
    const FfmpegExportOptions& opts,
    const VideoEncoderCreateContext& context = {});

} // namespace chronon3d::cli

#endif // CHRONON3D_CLI_VIDEO_EXPORT_COMMON_HPP
