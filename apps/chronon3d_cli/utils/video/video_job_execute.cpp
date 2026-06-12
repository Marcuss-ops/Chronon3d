// ---------------------------------------------------------------------------
// utils/video/video_job_execute.cpp — Phase 4: execute a validated VideoJobPlan
// ---------------------------------------------------------------------------

#include "video_job_plan.hpp"
#include "../../commands/video/exporter_registry.hpp"
#include <chronon3d/core/cancellation_token.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

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
    if (opts.output.empty() &&
        opts.sink_mode != VideoSinkMode::NullRender &&
        opts.sink_mode != VideoSinkMode::NullConvert) {
        spdlog::error("[video] No output path specified.");
        return 1;
    }
    if (opts.sink_mode == VideoSinkMode::Ffmpeg && !ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH.");
        return 1;
    }
    if (end <= start) {
        spdlog::error("[video] Empty frame range [{}, {})", start, end);
        return 1;
    }
    if (opts.ffmpeg_mode != "png" && opts.ffmpeg_mode != "pipe") {
        spdlog::error("[video] Unknown --ffmpeg-mode '{}'. Expected: png, pipe",
                      opts.ffmpeg_mode);
        return 1;
    }
    if (opts.encoder_backend == "native" && opts.ffmpeg_mode != "pipe") {
        spdlog::error("[video] --encoder-backend native requires --ffmpeg-mode pipe");
        return 1;
    }

    auto* exporter = shared_exporter_registry().find(opts.ffmpeg_mode);
    if (!exporter) {
        spdlog::error("[video] Unknown ffmpeg-mode '{}'. Expected one of: pipe, png",
                      opts.ffmpeg_mode);
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
        FfmpegExportOptions opts;
        opts.output            = plan.output.output;
        opts.frames_dir_name   = plan.output.frames_dir_name;
        opts.fps               = plan.output.fps;
        opts.codec             = plan.encoder.codec;
        opts.hardware_encoder  = plan.encoder.hardware_encoder;
        opts.encode_preset     = plan.encoder.encode_preset;
        opts.tune              = plan.encoder.tune;
        opts.crf               = plan.encoder.crf;
        opts.encoder_backend   = plan.encoder.encoder_backend;
        opts.pipe_pixfmt       = plan.pipe.pipe_pixfmt;
        opts.pipe_writer       = plan.pipe.pipe_writer;
        opts.color_output      = plan.pipe.color_output;
        opts.ffmpeg_verbose    = plan.pipe.ffmpeg_verbose;
        opts.warmup_renderer     = plan.warmup.warmup_renderer;
        opts.warmup_framebuffers = plan.warmup.warmup_framebuffers;
        opts.warmup_dummy_frame  = plan.warmup.warmup_dummy_frame;
        opts.keep_frames       = plan.sink.keep_frames;
        opts.chunks            = plan.sink.chunks;

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

    // ── Legacy FFmpeg path (pipe / png) ────────────────────────────────
    // Validation already done by command_video() via validate_video_job().
    // render_and_encode_ffmpeg() still validates as a safety net for callers.

    // ── Graceful cancellation (SIGINT/SIGTERM) ──
    chronon3d::CancellationToken cancel_token;
    install_signal_cancellation(cancel_token);

    // Attach cancellation token to options (non-const copy)
    FfmpegExportOptions opts = plan.export_options;
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
