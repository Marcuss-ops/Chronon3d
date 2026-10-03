#include "video_export_support.hpp"
#include "../../commands/video/common/video_export_common.hpp"

#include <utility>

namespace chronon3d::cli {

FfmpegExportOptions make_ffmpeg_export_options(const RenderJob& job) {
    OutputOptions output;
    output.output = job.output;
    output.frames_dir_name = job.video_settings.frames_dir;
    // F3.1 — rational-only frame rate: fps_num/fps_den are copied verbatim
    // (the scalar fps alias was removed from VideoSettings and OutputOptions).
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
    pipe.ocio_config_path = job.video_settings.ocio_config_path;
    pipe.ocio_source_space = job.video_settings.ocio_source_space;
    pipe.ocio_display = job.video_settings.ocio_display;
    pipe.ocio_view = job.video_settings.ocio_view;
    pipe.reference_white_nits = job.video_settings.reference_white_nits;
    pipe.hdr_mastering_metadata = job.video_settings.hdr_mastering_metadata;
    pipe.ffmpeg_verbose = job.video_settings.ffmpeg_verbose;

    RenderWarmupOptions warmup;
    warmup.warmup_renderer = job.execution.warmup_renderer;
    warmup.warmup_framebuffers = job.execution.warmup_framebuffers;
    warmup.warmup_dummy_frame = job.execution.warmup_dummy_frame;

    SinkOptions sink;
    // The canonical field is the authority; the CLI job validator has already
    // fail-closed on an unknown name, so an unparsable value here cannot be
    // silently reinterpreted as the encoder path.
    sink.sink_type = try_parse_video_sink_type(job.video_settings.sink_type)
                         .value_or(VideoSinkType::Ffmpeg);
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
    opts.audio_source_path = job.video_settings.audio_source_path;
    opts.audio_target_sample_rate = job.video_settings.audio_target_sample_rate;
    opts.gop_copy_only = job.video_settings.gop_copy_only;
    opts.changed_intervals = job.video_settings.changed_intervals;
    opts.backend_preference = job.execution.config
        ? job.execution.config->backend_preference()
        : chronon3d::graph::BackendPreference::Auto;
    opts.gpu_hot_path_mode = job.execution.config
        ? job.execution.config->gpu_hot_path_mode()
        : chronon3d::GpuHotPathMode::Auto;
    return opts;
}

} // namespace chronon3d::cli
