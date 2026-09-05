#include "video_export_support.hpp"
#include "../../commands/video/common/video_export_common.hpp"

#include <utility>

namespace chronon3d::cli {

FfmpegExportOptions make_ffmpeg_export_options(const RenderJob& job) {
    OutputOptions output;
    output.output = job.output;
    output.frames_dir_name = job.video_settings.frames_dir;
    output.fps = job.video_settings.fps;
    output.fps_num = job.video_settings.fps_num;
    output.fps_den = job.video_settings.fps_den;

    EncoderOptions encoder;
    encoder.codec = job.video_settings.codec;
    encoder.hardware_encoder = job.video_settings.hardware_encoder;
    encoder.encode_preset = job.video_settings.encode_preset;
    encoder.encode_preset_explicit = job.video_settings.encode_preset_explicit;
    encoder.tune = job.video_settings.tune;
    encoder.tune_explicit = job.video_settings.tune_explicit;
    encoder.rate_control_mode = job.video_settings.rate_control_mode;
    encoder.rate_control_mode_explicit = job.video_settings.rate_control_mode_explicit;
    encoder.crf = job.video_settings.crf;
    encoder.crf_explicit = job.video_settings.crf_explicit;
    encoder.qp = job.video_settings.qp;
    encoder.qp_explicit = job.video_settings.qp_explicit;
    encoder.bitrate = job.video_settings.bitrate;
    encoder.bitrate_explicit = job.video_settings.bitrate_explicit;
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
    opts.assets_root = job.execution.assets_root;
    opts.gop_source = job.video_settings.gop_source;
    opts.gop_copy_only = job.video_settings.gop_copy_only;
    opts.backend_preference = job.execution.config
        ? job.execution.config->backend_preference()
        : chronon3d::graph::BackendPreference::Auto;
    opts.gpu_hot_path_mode = job.execution.config
        ? job.execution.config->gpu_hot_path_mode()
        : chronon3d::GpuHotPathMode::Auto;
    return opts;
}

} // namespace chronon3d::cli
