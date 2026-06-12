// ---------------------------------------------------------------------------
// utils/video/video_job_plan.cpp — Phase 1: build VideoJobPlan from VideoArgs
// ---------------------------------------------------------------------------

#include "video_job_plan.hpp"
#include "../../commands.hpp"
#include "../../utils/common/cli_utils.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace chronon3d::cli {

std::optional<VideoJobPlan> plan_video_job(
    const CompositionRegistry& registry,
    const VideoArgs& args)
{
    if (args.comp_id.empty()) {
        spdlog::error("[video] No composition specified.");
        return std::nullopt;
    }

    const std::string video_output = args.output.empty()
        ? chronon_artifact_path("videos",
            std::filesystem::path(args.comp_id).filename().string() + ".mp4")
              .string()
        : args.output;

    // ── Resolve composition ─────────────────────────────────────────────
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return std::nullopt;

    const auto& comp = *resolved.comp;

    // ── RenderSettings ──────────────────────────────────────────────────
    RenderSettings settings = settings_from_args(args, !resolved.from_specscene);

    // ── CLI end is EXCLUSIVE (documented semantic [start, end)) ─────────
    const Frame end = (args.end > args.start)
        ? args.end
        : comp.duration();

    // ── FfmpegExportOptions ─────────────────────────────────────────────
    FfmpegExportOptions opts;
    opts.output            = video_output;
    opts.frames_dir_name   = args.frames_dir.empty()
        ? ("chronon_" + std::filesystem::path(args.comp_id).filename().string())
        : args.frames_dir;
    opts.fps               = args.fps;
    opts.crf               = args.crf;
    opts.codec             = args.codec;
    opts.hardware_encoder  = args.hardware_encoder;
    opts.encode_preset     = args.encode_preset;
    opts.tune              = args.tune;
    opts.keep_frames       = args.keep_frames;
    opts.chunks            = args.chunks;
    opts.ffmpeg_mode       = args.ffmpeg_mode;
    opts.ffmpeg_verbose    = args.ffmpeg_verbose;
    opts.pipe_pixfmt       = args.pipe_pixfmt;
    opts.color_output      = args.color_output;
    opts.pipe_writer       = args.pipe_writer;
    opts.encoder_backend   = args.encoder_backend;
    opts.video_sink        = args.video_sink;
    opts.sink_mode         = parse_video_sink_mode(args.video_sink);

    // ── Auto-tuning ─────────────────────────────────────────────────────
    if (opts.tune.empty() && opts.codec == "libx264") {
        opts.tune = "zerolatency";
        spdlog::info("[video] Auto-selecting x264 tune=zerolatency for low-latency pipe export");
    }

#if defined(__linux__)
    if (args.pipe_pixfmt == "rgba" &&
        comp.width() % 2 == 0 && comp.height() % 2 == 0 &&
        args.codec != "libx264rgb")
    {
        opts.pipe_pixfmt = "yuv420p";
        spdlog::info("[video] Auto-selecting yuv420p pipe pixel format for {}x{} output",
                     comp.width(), comp.height());
    }
#endif

#ifdef __linux__
    if (opts.pipe_writer == "io_uring") {
        spdlog::warn("[video] io_uring pipe writer is experimental — "
                      "output may be corrupted on some kernels; "
                      "use --pipe-writer classic for stable exports");
    }
#endif

    opts.warmup_renderer     = args.pipeline.warmup_renderer || (args.ffmpeg_mode == "pipe");
    opts.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    opts.warmup_dummy_frame  = args.pipeline.warmup_dummy_frame || (args.ffmpeg_mode == "pipe");

    return VideoJobPlan{
        .registry       = &registry,
        .comp           = resolved.comp,
        .comp_id        = args.comp_id,
        .settings       = settings,
        .export_options = opts,
        .start          = args.start,
        .end_exclusive  = end,
        .dry_run        = args.dry_run,
        .from_specscene = resolved.from_specscene,
    };
}

} // namespace chronon3d::cli
