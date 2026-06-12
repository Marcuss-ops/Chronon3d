// ---------------------------------------------------------------------------
// utils/video/video_job_validate.cpp — Phase 2: validate a VideoJobPlan
// ---------------------------------------------------------------------------

#include "video_job_plan.hpp"
#include "../../commands/video/exporter_registry.hpp"
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

bool validate_video_job(const VideoJobPlan& plan) {
    const auto& opts = plan.export_options;

    if (opts.output.empty() &&
        opts.sink_mode != VideoSinkMode::NullRender &&
        opts.sink_mode != VideoSinkMode::NullConvert) {
        spdlog::error("[video] No output path specified.");
        return false;
    }

    // Only require ffmpeg in PATH for actual ffmpeg sink mode
    if (opts.sink_mode == VideoSinkMode::Ffmpeg && !ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH.");
        return false;
    }

    if (plan.end_exclusive <= plan.start) {
        spdlog::error("[video] Empty frame range [{}, {})",
                      plan.start, plan.end_exclusive);
        return false;
    }

    if (opts.ffmpeg_mode != "png" && opts.ffmpeg_mode != "pipe") {
        spdlog::error("[video] Unknown --ffmpeg-mode '{}'. Expected: png, pipe",
                      opts.ffmpeg_mode);
        return false;
    }

    if (opts.encoder_backend == "native" && opts.ffmpeg_mode != "pipe") {
        spdlog::error("[video] --encoder-backend native requires --ffmpeg-mode pipe");
        return false;
    }

    // Verify the exporter exists via shared registry
    auto* exporter = shared_exporter_registry().find(opts.ffmpeg_mode);
    if (!exporter) {
        spdlog::error("[video] Unknown ffmpeg-mode '{}'. Expected one of: pipe, png",
                      opts.ffmpeg_mode);
        return false;
    }

    return true;
}

} // namespace chronon3d::cli
