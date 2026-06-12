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

    // ── Focused sub-option structs ───────────────────────────────────────
    OutputOptions output_opts;
    output_opts.output          = video_output;
    output_opts.frames_dir_name = args.frames_dir.empty()
        ? ("chronon_" + std::filesystem::path(args.comp_id).filename().string())
        : args.frames_dir;
    output_opts.fps             = args.fps;

    EncoderOptions encoder_opts;
    encoder_opts.codec           = args.codec;
    encoder_opts.hardware_encoder = args.hardware_encoder;
    encoder_opts.encode_preset   = args.encode_preset;
    encoder_opts.tune            = args.tune;
    encoder_opts.crf             = args.crf;
    encoder_opts.encoder_backend = args.encoder_backend;

    PipeOptions pipe_opts;
    pipe_opts.pipe_pixfmt    = args.pipe_pixfmt;
    pipe_opts.pipe_writer    = args.pipe_writer;
    pipe_opts.color_output   = args.color_output;
    pipe_opts.ffmpeg_verbose = args.ffmpeg_verbose;

    SinkOptions sink_opts;
    // Parse sink mode: "ffmpeg", "null-render", "null-convert"
    if (args.sink_mode == "null-render") {
        sink_opts.sink_type = VideoSinkType::NullRender;
    } else if (args.sink_mode == "null-convert") {
        sink_opts.sink_type = VideoSinkType::NullConvert;
    } else {
        sink_opts.sink_type = VideoSinkType::Ffmpeg;  // default
    }
    sink_opts.ffmpeg_mode = args.ffmpeg_mode;
    sink_opts.keep_frames = args.keep_frames;
    sink_opts.chunks      = args.chunks;

    RenderWarmupOptions warmup_opts;
    warmup_opts.warmup_renderer     = args.pipeline.warmup_renderer || (args.ffmpeg_mode == "pipe");
    warmup_opts.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    warmup_opts.warmup_dummy_frame  = args.pipeline.warmup_dummy_frame || (args.ffmpeg_mode == "pipe");

    // ── Auto-tuning ─────────────────────────────────────────────────────
    if (encoder_opts.tune.empty() && encoder_opts.codec == "libx264") {
        encoder_opts.tune = "zerolatency";
        spdlog::info("[video] Auto-selecting x264 tune=zerolatency for low-latency pipe export");
    }

#if defined(__linux__)
    if (args.pipe_pixfmt == "rgba" &&
        comp.width() % 2 == 0 && comp.height() % 2 == 0 &&
        args.codec != "libx264rgb")
    {
        pipe_opts.pipe_pixfmt = "yuv420p";
        spdlog::info("[video] Auto-selecting yuv420p pipe pixel format for {}x{} output",
                     comp.width(), comp.height());
    }
#endif

#ifdef __linux__
    if (pipe_opts.pipe_writer == "io_uring") {
        spdlog::warn("[video] io_uring pipe writer is experimental — "
                      "output may be corrupted on some kernels; "
                      "use --pipe-writer classic for stable exports");
    }
#endif

    warmup_opts.warmup_renderer     = args.pipeline.warmup_renderer || (args.ffmpeg_mode == "pipe");
    warmup_opts.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    warmup_opts.warmup_dummy_frame  = args.pipeline.warmup_dummy_frame || (args.ffmpeg_mode == "pipe");

    return VideoJobPlan{
        .registry       = &registry,
        .comp           = resolved.comp,
        .comp_id        = args.comp_id,
        .settings       = settings,
        .output         = output_opts,
        .encoder        = encoder_opts,
        .pipe           = pipe_opts,
        .warmup         = warmup_opts,
        .sink           = sink_opts,
        .start          = args.start,
        .end_exclusive  = end,
        .dry_run        = args.dry_run,
        .from_specscene = resolved.from_specscene,
    };
}

} // namespace chronon3d::cli
