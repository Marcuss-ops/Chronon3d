// ---------------------------------------------------------------------------
// utils/video/video_job_execute.cpp — Phase 4: execute a validated VideoJobPlan
// ---------------------------------------------------------------------------

#include "video_job_plan.hpp"
#include "../../commands/video/exporter_registry.hpp"
#include <chronon3d/core/cancellation_token.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

// ── Helper: assemble FfmpegExportOptions from focused sub-option structs ────
// Single point where the legacy flat options struct is populated, so both
// the FFmpeg and null-sink paths stay in sync.

[[nodiscard]] static FfmpegExportOptions make_ffmpeg_opts(const VideoJobPlan& plan) {
    FfmpegExportOptions opts;
    opts.output    = plan.output;
    opts.encoder   = plan.encoder;
    opts.pipe      = plan.pipe;
    opts.warmup    = plan.warmup;
    opts.sink      = plan.sink;
    return opts;
}

// ── Shared exporter registry (single instance across validate + execute) ────

ExporterRegistry& shared_exporter_registry() {
    static ExporterRegistry reg = []() {
        ExporterRegistry r;
        register_builtin_exporters(r);
        return r;
    }();
    return reg;
}

// ── Shared render + encode dispatch ─────────────────────────────────────────

int render_and_encode_ffmpeg(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts)
{
    // Safety-net validation for direct callers (e.g. command_video_camera)
    // that bypass validate_video_job().
    if (opts.output.output.empty() &&
        opts.sink.sink_type != VideoSinkType::NullRender &&
        opts.sink.sink_type != VideoSinkType::NullConvert) {
        spdlog::error("[video] No output path specified.");
        return 1;
    }
    if (opts.sink.sink_type == VideoSinkType::Ffmpeg && !ffmpeg_in_path()) {
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

    auto* exporter = shared_exporter_registry().find(opts.sink.ffmpeg_mode);
    if (!exporter) {
        spdlog::error("[video] Unknown ffmpeg-mode '{}'. Expected one of: pipe, png",
                      opts.sink.ffmpeg_mode);
        return 1;
    }

    const VideoExportJob job{
        .registry       = registry,
        .comp           = comp,
        .composition_id = composition_id,
        .settings       = settings,
        .start          = start,
        .end            = end,
        .opts           = opts,
    };
    return exporter->export_video(job);
}

// ── Phase 4 — Execute ───────────────────────────────────────────────────────

int execute_video_job(const VideoJobPlan& plan) {
    // ── Sink-based dispatch ────────────────────────────────────────────
    // Null sinks (null-render, null-convert) are for benchmarking and
    // bypass the FFmpeg path entirely.  They use a simplified VideoExportJob
    // constructed from the plan's sub-option structs.
    if (plan.sink.sink_type != VideoSinkType::Ffmpeg) {
        const auto sink_id = std::string(to_string(plan.sink.sink_type));
        auto* exporter = shared_exporter_registry().find(sink_id);
        if (!exporter) {
            spdlog::error("[video] Unknown sink '{}'", sink_id);
            return 1;
        }

        // Build VideoExportJob from sub-option structs
        auto opts = make_ffmpeg_opts(plan);

        const VideoExportJob job{
            .registry       = *plan.registry,
            .comp           = *plan.comp,
            .composition_id = plan.comp_id,
            .settings       = plan.settings,
            .start          = plan.start,
            .end            = plan.end_exclusive,
            .opts           = opts,
        };
        return exporter->export_video(job);
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
        opts);
}

} // namespace chronon3d::cli
