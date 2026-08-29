#include "video_export_support.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

bool validate_video_job(const RenderJob& job) {
    if (job.mode != RenderMode::Video) {
        spdlog::error("[video] RenderJob mode is not Video.");
        return false;
    }
    if (!job.registry || !job.compiled || !job.compiled->composition) {
        spdlog::error("[video] RenderJob is missing registry or compiled composition.");
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
    if (encoder_backend_requires_ffmpeg(job.video_settings.encoder_backend) &&
        !ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH (required by the pipe encoder).");
        return false;
    }
    if (job.last_frame < job.first_frame) {
        spdlog::error("[video] Empty frame range [{}, {}]",
                      job.first_frame, job.last_frame);
        return false;
    }
    if (job.video_settings.ffmpeg_mode != "pipe") {
        spdlog::error(
            "[video] Unsupported ffmpeg mode '{}'. Only 'pipe' is available",
            job.video_settings.ffmpeg_mode);
        return false;
    }
    if (job.video_settings.gop_copy_only) {
        if (job.video_settings.gop_source.empty()) {
            spdlog::error("[video] gop_copy_only requires a non-empty gop_source.");
            return false;
        }
        spdlog::error("[video] gop_copy_only is unavailable without the legacy PNG/chunked exporter.");
        return false;
    }
    return true;
}

} // namespace chronon3d::cli
