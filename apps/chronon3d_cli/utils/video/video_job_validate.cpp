#include "video_export_support.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

bool validate_video_job(const RenderJob& job) {
    if (job.mode != RenderMode::Video) {
        spdlog::error("[video] RenderJob mode is not Video.");
        return false;
    }
    if (!job.registry || !job.comp) {
        spdlog::error("[video] RenderJob is missing registry or composition.");
        return false;
    }
    if (job.output.empty()) {
        spdlog::error("[video] No output path specified.");
        return false;
    }
    if (job.video_settings.sink_type != "ffmpeg") {
        spdlog::error(
            "[video] Non-FFmpeg sink types are not supported. "
            "Use 'bench' for benchmarking.");
        return false;
    }
    if (!ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH.");
        return false;
    }
    if (job.last_frame < job.first_frame) {
        spdlog::error("[video] Empty frame range [{}, {}]",
                      job.first_frame, job.last_frame);
        return false;
    }
    if (job.video_settings.ffmpeg_mode != "png" &&
        job.video_settings.ffmpeg_mode != "pipe") {
        spdlog::error(
            "[video] Unknown ffmpeg mode '{}'. Expected: png, pipe",
            job.video_settings.ffmpeg_mode);
        return false;
    }
    if (job.video_settings.encoder_backend == "native" &&
        job.video_settings.ffmpeg_mode != "pipe") {
        spdlog::error(
            "[video] Native encoder backend requires ffmpeg mode pipe");
        return false;
    }
    return true;
}

} // namespace chronon3d::cli
