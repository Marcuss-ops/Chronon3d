// ---------------------------------------------------------------------------
// utils/video/video_job_validate.cpp — Phase 2: validate a VideoJobPlan
// ---------------------------------------------------------------------------

#include "video_job_plan.hpp"
#include "../../commands/video/exporter_registry.hpp"
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

bool validate_video_job(const VideoJobPlan& plan) {

    if (plan.output.output.empty()) {
        spdlog::error("[video] No output path specified.");
        return false;
    }

    // For FFmpeg sinks check ffmpeg availability; null sinks don't need it.
    if (plan.sink.sink_type == VideoSinkType::Ffmpeg && !ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH.");
        return false;
    }

    if (plan.end_exclusive <= plan.start) {
        spdlog::error("[video] Empty frame range [{}, {})",
                      plan.start, plan.end_exclusive);
        return false;
    }

    // Validate sink-specific options
    if (plan.sink.sink_type == VideoSinkType::Ffmpeg) {
        if (plan.sink.ffmpeg_mode != "png" && plan.sink.ffmpeg_mode != "pipe") {
            spdlog::error("[video] Unknown --ffmpeg-mode '{}'. Expected: png, pipe",
                          plan.sink.ffmpeg_mode);
            return false;
        }

        if (plan.encoder.encoder_backend == "native" && plan.sink.ffmpeg_mode != "pipe") {
            spdlog::error("[video] --encoder-backend native requires --ffmpeg-mode pipe");
            return false;
        }

        // Verify the exporter exists via shared registry
        auto* exporter = shared_exporter_registry().find(plan.sink.ffmpeg_mode);
        if (!exporter) {
            spdlog::error("[video] Unknown ffmpeg-mode '{}'. Expected one of: pipe, png",
                          plan.sink.ffmpeg_mode);
            return false;
        }
    } else {
        // Verify the sink exporter exists
        auto sink_id = to_string(plan.sink.sink_type);
        auto* exporter = shared_exporter_registry().find(sink_id);
        if (!exporter) {
            spdlog::error("[video] Unknown sink '{}'. Expected one of: ffmpeg, null-render, null-convert",
                          sink_id);
            return false;
        }
    }

    return true;
}

} // namespace chronon3d::cli
