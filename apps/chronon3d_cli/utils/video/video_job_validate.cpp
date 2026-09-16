#include "video_export_support.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <system_error>

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
    // Sink axis. Parsed with the CLI's ONE vocabulary instead of a string
    // comparison, so a new sink name cannot pass here and then be dropped by
    // the options factory. Two sinks are executable: `ffmpeg` (render → encode
    // → mux) and `raw` (render → pre-encode frames, no encoder). The null
    // sinks exist for benchmarking only: running one as an export would report
    // success while producing no artifact, so they stay refused here.
    const auto sink = try_parse_video_sink_type(job.video_settings.sink_type);
    if (!sink) {
        spdlog::error("[video] Unknown sink '{}'. Accepted: {}",
                      job.video_settings.sink_type, video_sink_names_joined());
        return false;
    }
    if (!video_sink_type_is_executable(*sink)) {
        spdlog::error(
            "[video] Sink '{}' is benchmark-only and cannot be an export target. "
            "Accepted: {}",
            job.video_settings.sink_type, video_sink_names_joined());
        return false;
    }
    // A pre-encode sink writes the rendered host frames itself: no encoder, no
    // muxer, and therefore no external ffmpeg requirement and no ffmpeg-mode
    // constraint. Only the encoding sink owns those two preconditions.
    if (video_sink_type_encodes(*sink)) {
        if (encoder_backend_requires_ffmpeg(job.video_settings.encoder_backend) &&
            !ffmpeg_in_path()) {
            spdlog::error("[video] ffmpeg not found in PATH (required by the pipe encoder).");
            return false;
        }
    }
    if (job.last_frame < job.first_frame) {
        spdlog::error("[video] Empty frame range [{}, {}]",
                      job.first_frame, job.last_frame);
        return false;
    }
    if (video_sink_type_encodes(*sink) &&
        job.video_settings.ffmpeg_mode != "pipe") {
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
        // Packet copy is resolved by the canonical video execution resolver;
        // do not reject it at generic job validation time.
    }
    // A supplied compressed source must be readable before any resolver
    // downgrade can silently swap the requested lane for a different plan.
    if (!job.video_settings.gop_source.empty()) {
        std::error_code ec;
        const bool readable =
            std::filesystem::is_regular_file(job.video_settings.gop_source, ec) &&
            !ec;
        if (!readable) {
            spdlog::error("[video] gop_source '{}' is not a readable regular file (ec={})",
                          job.video_settings.gop_source,
                          ec ? ec.message() : "none");
            return false;
        }
    }
    return true;
}

} // namespace chronon3d::cli
