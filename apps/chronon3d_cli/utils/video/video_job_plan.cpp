#include "video_job_plan.hpp"
#include "../../commands.hpp"
#include "../../utils/common/cli_utils.hpp"
#include "../../utils/job/cli_render_utils.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>

namespace chronon3d::cli {

std::optional<RenderJob> make_video_render_job(
    const CompositionRegistry& registry,
    const VideoArgs& args)
{
    if (args.comp_id.empty()) {
        spdlog::error("[video] No composition specified.");
        return std::nullopt;
    }

    const std::string output = args.output.empty()
        ? chronon_artifact_path(
              "videos",
              std::filesystem::path(args.comp_id).filename().string() + ".mp4")
              .string()
        : args.output;

    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return std::nullopt;

    const auto& comp = *resolved.comp;
    const Frame end_exclusive = (args.end > args.start)
        ? args.end
        : comp.duration();

    RenderJob job = RenderJob::video_job(
        args.comp_id,
        resolved.comp,
        args.start,
        Frame{end_exclusive.integral() - 1},
        output);
    job.registry = &registry;
    job.settings = settings_from_args(args);

    job.video_settings.fps = args.fps;
    job.video_settings.crf = args.crf;
    job.video_settings.codec = args.codec;
    job.video_settings.encode_preset = args.encode_preset;
    job.video_settings.tune = args.tune;
    job.video_settings.keep_frames = args.keep_frames;
    job.video_settings.frames_dir = args.frames_dir.empty()
        ? ("chronon_" + std::filesystem::path(args.comp_id).filename().string())
        : args.frames_dir;
    job.video_settings.chunks = args.chunks;
    job.video_settings.hardware_encoder = args.hardware_encoder;
    job.video_settings.ffmpeg_mode = args.ffmpeg_mode;
    job.video_settings.ffmpeg_verbose = args.ffmpeg_verbose;
    job.video_settings.pipe_pixfmt = args.pipe_pixfmt;
    job.video_settings.color_output = args.color_output;
    job.video_settings.pipe_writer = args.pipe_writer;
    job.video_settings.encoder_backend = args.encoder_backend;
    job.video_settings.sink_type = "ffmpeg";
    job.video_settings.dry_run = args.dry_run;

    job.execution.warmup_renderer =
        args.pipeline.warmup_renderer || args.ffmpeg_mode == "pipe";
    job.execution.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    job.execution.warmup_dummy_frame =
        args.pipeline.warmup_dummy_frame || args.ffmpeg_mode == "pipe";
    job.execution.cpu_budget = args.cpu_budget;

    if (job.video_settings.tune.empty() &&
        job.video_settings.codec == "libx264") {
        job.video_settings.tune = "zerolatency";
        spdlog::info(
            "[video] Auto-selecting x264 tune=zerolatency for low-latency pipe export");
    }

#if defined(__linux__)
    if (job.video_settings.pipe_pixfmt == "rgba" &&
        comp.width() % 2 == 0 && comp.height() % 2 == 0 &&
        job.video_settings.codec != "libx264rgb") {
        job.video_settings.pipe_pixfmt = "yuv420p";
        spdlog::info(
            "[video] Auto-selecting yuv420p pipe pixel format for {}x{} output",
            comp.width(), comp.height());
    }

    if (job.video_settings.pipe_writer == "io_uring") {
        spdlog::warn(
            "[video] io_uring pipe writer is experimental — output may be "
            "corrupted on some kernels; use --pipe-writer classic for stable exports");
    }
#endif

    return job;
}

} // namespace chronon3d::cli
