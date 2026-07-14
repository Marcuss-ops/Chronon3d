// ---------------------------------------------------------------------------
// utils/video/video_job_execute.cpp — Phase 4: execute a validated VideoJobPlan
// ---------------------------------------------------------------------------

#include "video_job_plan.hpp"
#include "../../commands/video/common/pipe_export_pipeline.hpp"
#include "../../commands/video/common/video_export_common.hpp"
#include <chronon3d/core/cancellation_token.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

// ── Helper: assemble FfmpegExportOptions from focused sub-option structs ────
// Single point where the legacy flat options struct is populated.

[[nodiscard]] static FfmpegExportOptions make_ffmpeg_opts(const VideoJobPlan& plan) {
    FfmpegExportOptions opts;
    opts.output    = plan.output;
    opts.encoder   = plan.encoder;
    opts.pipe      = plan.pipe;
    opts.warmup    = plan.warmup;
    opts.sink      = plan.sink;
    return opts;
}

// ── Shared render + encode dispatch ─────────────────────────────────────────

int render_and_encode_ffmpeg(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget)
{
    // Safety-net validation for direct callers (e.g. command_video_camera)
    // that bypass validate_video_job().
    if (opts.output.output.empty()) {
        spdlog::error("[video] No output path specified.");
        return 1;
    }
    if (!ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH.");
        return 1;
    }
    if (end <= start) {
        spdlog::error("[video] Empty frame range [{}, {})", start, end);
        return 1;
    }
    if (opts.sink.ffmpeg_mode != "png" && opts.sink.ffmpeg_mode != "pipe") {
        spdlog::error("[video] Unknown --ffmpeg-mode '{}'. Expected: png, pipe",
                      opts.sink.ffmpeg_mode);
        return 1;
    }
    if (opts.encoder.encoder_backend == "native" && opts.sink.ffmpeg_mode != "pipe") {
        spdlog::error("[video] --encoder-backend native requires --ffmpeg-mode pipe");
        return 1;
    }

    // Direct dispatch: pipe → render_and_encode_ffmpeg_pipe, png → chunked
    if (opts.sink.ffmpeg_mode == "pipe") {
        auto result = render_and_encode_ffmpeg_pipe(
            registry, comp, composition_id,
            settings, start, end, opts, cpu_budget);
        return result.return_code;
    }
    if (opts.sink.ffmpeg_mode == "png") {
        auto result = render_and_encode_ffmpeg_chunked(
            registry, comp, composition_id,
            settings, start, end, opts, cpu_budget);
        return result.return_code;
    }

    spdlog::error("[video] Unknown ffmpeg-mode '{}'. Expected one of: pipe, png",
                  opts.sink.ffmpeg_mode);
    return 1;
}

// ── Phase 4 — Execute ───────────────────────────────────────────────────────

int execute_video_job(const VideoJobPlan& plan) {
    // Only FFmpeg sinks are supported (null-render/null-convert removed —
    // use `bench` and `dev bench-convert` for benchmarking).
    if (plan.sink.sink_type != VideoSinkType::Ffmpeg) {
        spdlog::error("[video] Non-FFmpeg sink types not supported. Use 'bench' command for benchmarking.");
        return 1;
    }

    // ── FFmpeg path (pipe / png) ───────────────────────────────────────
    auto opts = make_ffmpeg_opts(plan);

    // ── Graceful cancellation (SIGINT/SIGTERM) ──
    chronon3d::CancellationToken cancel_token;
    install_signal_cancellation(cancel_token);
    opts.cancellation_token = &cancel_token;

    return render_and_encode_ffmpeg(
        *plan.registry,
        *plan.comp,
        plan.comp_id,
        plan.settings,
        plan.start,
        plan.end_exclusive,
        opts,
        plan.cpu_budget);
}

} // namespace chronon3d::cli
