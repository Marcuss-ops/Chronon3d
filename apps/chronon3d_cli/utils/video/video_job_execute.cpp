#include "video_job_plan.hpp"
#include "../../commands/video/common/pipe_export_pipeline.hpp"
#include "../../commands/video/common/video_export_common.hpp"

#include <chronon3d/core/cancellation_token.hpp>

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

namespace {

[[nodiscard]] FfmpegExportOptions make_ffmpeg_opts(const RenderJob& job) {
    OutputOptions output;
    output.output = job.output;
    output.frames_dir_name = job.video_settings.frames_dir;
    output.fps = job.video_settings.fps;

    EncoderOptions encoder;
    encoder.codec = job.video_settings.codec;
    encoder.hardware_encoder = job.video_settings.hardware_encoder;
    encoder.encode_preset = job.video_settings.encode_preset;
    encoder.tune = job.video_settings.tune;
    encoder.crf = job.video_settings.crf;
    encoder.encoder_backend = job.video_settings.encoder_backend;

    PipeOptions pipe;
    pipe.pipe_pixfmt = job.video_settings.pipe_pixfmt;
    pipe.pipe_writer = job.video_settings.pipe_writer;
    pipe.color_output = job.video_settings.color_output;
    pipe.ffmpeg_verbose = job.video_settings.ffmpeg_verbose;

    RenderWarmupOptions warmup;
    warmup.warmup_renderer = job.execution.warmup_renderer;
    warmup.warmup_framebuffers = job.execution.warmup_framebuffers;
    warmup.warmup_dummy_frame = job.execution.warmup_dummy_frame;

    SinkOptions sink;
    sink.sink_type = VideoSinkType::Ffmpeg;
    sink.ffmpeg_mode = job.video_settings.ffmpeg_mode;
    sink.keep_frames = job.video_settings.keep_frames;
    sink.chunks = job.video_settings.chunks;

    FfmpegExportOptions opts;
    opts.output = std::move(output);
    opts.encoder = std::move(encoder);
    opts.pipe = std::move(pipe);
    opts.warmup = std::move(warmup);
    opts.sink = std::move(sink);
    return opts;
}

} // namespace

int render_and_encode_ffmpeg(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget)
{
    if (opts.output.output.empty()) {
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
    if (opts.sink.ffmpeg_mode != "png" && opts.sink.ffmpeg_mode != "pipe") {
        spdlog::error("[video] Unknown ffmpeg mode '{}'. Expected: png, pipe",
                      opts.sink.ffmpeg_mode);
        return 1;
    }
    if (opts.encoder.encoder_backend == "native" &&
        opts.sink.ffmpeg_mode != "pipe") {
        spdlog::error("[video] Native encoder backend requires ffmpeg mode pipe");
        return 1;
    }

    if (opts.sink.ffmpeg_mode == "pipe") {
        auto result = render_and_encode_ffmpeg_pipe(
            registry, comp, composition_id,
            settings, start, end, opts, cpu_budget);
        return result.return_code;
    }
    if (opts.sink.ffmpeg_mode == "png") {
        auto result = render_and_encode_ffmpeg_chunked(
            registry, comp, composition_id,
            settings, start, end, opts, cpu_budget);
        return result.return_code;
    }

    return 1;
}

int execute_video_job(const RenderJob& job) {
    if (!validate_video_job(job)) {
        return 1;
    }

    auto opts = make_ffmpeg_opts(job);

    chronon3d::CancellationToken cancel_token;
    install_signal_cancellation(cancel_token);
    opts.cancellation_token = &cancel_token;

    return render_and_encode_ffmpeg(
        *job.registry,
        *job.comp,
        job.comp_id,
        job.settings,
        job.first_frame,
        job.last_frame + Frame{1},
        opts,
        job.execution.cpu_budget);
}

} // namespace chronon3d::cli
