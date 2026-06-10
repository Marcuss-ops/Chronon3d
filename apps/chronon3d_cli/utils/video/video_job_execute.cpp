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
    // Validation (also protects direct callers like command_video_camera)
    if (opts.output.empty()) {
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
    // render_and_encode_ffmpeg validates internally — no double-check here

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
