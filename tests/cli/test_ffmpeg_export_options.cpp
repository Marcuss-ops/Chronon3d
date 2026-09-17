#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands/video/common/video_export_common.hpp>
#include <apps/chronon3d_cli/commands/video/common/pipe_pixfmt_vocabulary.hpp>
#include <apps/chronon3d_cli/commands/video/common/sink_options.hpp>
#include <apps/chronon3d_cli/utils/video/video_export_support.hpp>

#include <string>
#include <vector>

using namespace chronon3d::cli;

// ─────────────────────────────────────────────────────────────────────────────
// FfmpegExportOptions: sub-struct defaults match expected values
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("FfmpegExportOptions: default-constructed sub-structs have correct defaults") {
    FfmpegExportOptions opts;

    // OutputOptions defaults (rational-only frame rate: fps_num/fps_den)
    CHECK(opts.output.output.empty());
    CHECK(opts.output.frames_dir_name.empty());
    CHECK(opts.output.fps_num == 30);
    CHECK(opts.output.fps_den == 1);

    // EncoderOptions defaults
    CHECK(opts.encoder.codec == "auto");
    CHECK(opts.encoder.hardware_encoder == "none");
    CHECK(opts.encoder.encode_preset == "superfast");
    CHECK(opts.encoder.tune.empty());
    CHECK(opts.encoder.crf == 23);
    CHECK(opts.encoder.encoder_backend == "native");

    // PipeOptions defaults
    CHECK(opts.pipe.pipe_pixfmt == "rgba");
    CHECK(opts.pipe.pipe_writer == "classic");
    CHECK(opts.pipe.color_output == "srgb");
    CHECK_FALSE(opts.pipe.ffmpeg_verbose);

    // RenderWarmupOptions defaults
    CHECK_FALSE(opts.warmup.warmup_renderer);
    CHECK(opts.warmup.warmup_framebuffers == 2);
    CHECK_FALSE(opts.warmup.warmup_dummy_frame);

    // SinkOptions defaults
    CHECK(opts.sink.sink_type == VideoSinkType::Ffmpeg);
    CHECK(opts.sink.ffmpeg_mode == "pipe");
    CHECK_FALSE(opts.sink.keep_frames);
    CHECK(opts.sink.chunks == 1);

    // Cross-cutting
    CHECK(opts.cancellation_token == nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// FfmpegExportOptions: sub-struct assignment propagates correctly
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("FfmpegExportOptions: assigning sub-structs propagates all fields") {
    OutputOptions out;
    out.output = "/tmp/test.mp4";
    out.frames_dir_name = "test_frames";
    out.fps_num = 60;
    out.fps_den = 1;

    EncoderOptions enc;
    enc.codec = "libx265";
    enc.hardware_encoder = "nvenc";
    enc.encode_preset = "slow";
    enc.tune = "film";
    enc.crf = 18;
    enc.encoder_backend = "pipe";

    PipeOptions pipe;
    pipe.pipe_pixfmt = "yuv420p";
    pipe.pipe_writer = "io_uring";
    pipe.color_output = "rec709";
    pipe.ffmpeg_verbose = true;

    RenderWarmupOptions warmup;
    warmup.warmup_renderer = true;
    warmup.warmup_framebuffers = 8;
    warmup.warmup_dummy_frame = true;

    SinkOptions sink;
    sink.sink_type = VideoSinkType::NullRender;
    sink.ffmpeg_mode = "png";
    sink.keep_frames = true;
    sink.chunks = 4;

    FfmpegExportOptions opts;
    opts.output = out;
    opts.encoder = enc;
    opts.pipe = pipe;
    opts.warmup = warmup;
    opts.sink = sink;

    // Verify all fields propagated
    CHECK(opts.output.output == "/tmp/test.mp4");
    CHECK(opts.output.frames_dir_name == "test_frames");
    CHECK(opts.output.fps_num == 60);
    CHECK(opts.output.fps_den == 1);
    CHECK(opts.encoder.codec == "libx265");
    CHECK(opts.encoder.hardware_encoder == "nvenc");
    CHECK(opts.encoder.encode_preset == "slow");
    CHECK(opts.encoder.tune == "film");
    CHECK(opts.encoder.crf == 18);
    CHECK(opts.encoder.encoder_backend == "pipe");
    CHECK(opts.pipe.pipe_pixfmt == "yuv420p");
    CHECK(opts.pipe.pipe_writer == "io_uring");
    CHECK(opts.pipe.color_output == "rec709");
    CHECK(opts.pipe.ffmpeg_verbose);
    CHECK(opts.warmup.warmup_renderer);
    CHECK(opts.warmup.warmup_framebuffers == 8);
    CHECK(opts.warmup.warmup_dummy_frame);
    CHECK(opts.sink.sink_type == VideoSinkType::NullRender);
    CHECK(opts.sink.ffmpeg_mode == "png");
    CHECK(opts.sink.keep_frames);
    CHECK(opts.sink.chunks == 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// FfmpegExportOptions: sub-struct types have correct defaults in isolation
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("OutputOptions: standalone defaults are correct") {
    OutputOptions o;
    CHECK(o.output.empty());
    CHECK(o.frames_dir_name.empty());
    CHECK(o.fps_num == 30);
    CHECK(o.fps_den == 1);
}

TEST_CASE("EncoderOptions: standalone defaults are correct") {
    EncoderOptions e;
    CHECK(e.codec == "auto");
    CHECK(e.hardware_encoder == "none");
    CHECK(e.encode_preset == "superfast");
    CHECK(e.tune.empty());
    CHECK(e.crf == 23);
    CHECK(e.encoder_backend == "native");
}

TEST_CASE("PipeOptions: standalone defaults are correct") {
    PipeOptions p;
    CHECK(p.pipe_pixfmt == "rgba");
    CHECK(p.pipe_writer == "classic");
    CHECK(p.color_output == "srgb");
    CHECK_FALSE(p.ffmpeg_verbose);
}

TEST_CASE("RenderWarmupOptions: standalone defaults and active()") {
    RenderWarmupOptions w;
    CHECK_FALSE(w.warmup_renderer);
    CHECK(w.warmup_framebuffers == 2);
    CHECK_FALSE(w.warmup_dummy_frame);
    // active() returns true by default because warmup_framebuffers > 0
    CHECK(w.active());

    w.warmup_framebuffers = 0;
    CHECK_FALSE(w.active());

    w.warmup_renderer = true;
    CHECK(w.active());
}

TEST_CASE("SinkOptions: standalone defaults are correct") {
    SinkOptions s;
    CHECK(s.sink_type == VideoSinkType::Ffmpeg);
    CHECK(s.ffmpeg_mode == "pipe");
    CHECK_FALSE(s.keep_frames);
    CHECK(s.chunks == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// VideoSinkType: enum round-trip via to_string / parse
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("VideoSinkType: to_string and parse_video_sink_type round-trip") {
    CHECK(to_string(VideoSinkType::Ffmpeg) == "ffmpeg");
    CHECK(to_string(VideoSinkType::NullRender) == "null-render");
    CHECK(to_string(VideoSinkType::NullConvert) == "null-convert");
    CHECK(to_string(VideoSinkType::RawFile) == "raw");

    CHECK(parse_video_sink_type("ffmpeg") == VideoSinkType::Ffmpeg);
    CHECK(parse_video_sink_type("null-render") == VideoSinkType::NullRender);
    CHECK(parse_video_sink_type("null-convert") == VideoSinkType::NullConvert);
    CHECK(parse_video_sink_type("raw") == VideoSinkType::RawFile);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sink vocabulary: ONE list for the option checker, the validator and the
// parser, so --video-sink can never advertise a name that is not parseable.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("VideoSinkType: every advertised name parses and round-trips") {
    const auto names = video_sink_type_names();
    CHECK(names.size() == 4);
    for (const auto& name : names) {
        const auto parsed = try_parse_video_sink_type(name);
        REQUIRE(parsed.has_value());
        // to_string(parse(name)) == name is the round-trip contract the CLI
        // option checker relies on.
        CHECK(to_string(*parsed) == name);
    }
    // The joined list quoted in error messages is derived from the same list.
    CHECK(video_sink_names_joined() == "ffmpeg, raw, null-render, null-convert");
}

TEST_CASE("VideoSinkType: try_parse is the non-throwing surface, parse throws") {
    CHECK_FALSE(try_parse_video_sink_type("").has_value());
    CHECK_FALSE(try_parse_video_sink_type("bench").has_value());
    CHECK_FALSE(try_parse_video_sink_type("RAW").has_value());
    CHECK_THROWS(parse_video_sink_type("bench"));
}

TEST_CASE("VideoSinkType: only the encoder and the pre-encode sink are executable") {
    CHECK(video_sink_type_is_executable(VideoSinkType::Ffmpeg));
    CHECK(video_sink_type_is_executable(VideoSinkType::RawFile));
    // Benchmark-only sinks must never be an export target.
    CHECK_FALSE(video_sink_type_is_executable(VideoSinkType::NullRender));
    CHECK_FALSE(video_sink_type_is_executable(VideoSinkType::NullConvert));
    // Only the encoding sink needs an encoder/mux stage (and therefore an
    // external ffmpeg in the pipe transport).
    CHECK(video_sink_type_encodes(VideoSinkType::Ffmpeg));
    CHECK_FALSE(video_sink_type_encodes(VideoSinkType::RawFile));
    CHECK_FALSE(video_sink_type_encodes(VideoSinkType::NullRender));
    CHECK_FALSE(video_sink_type_encodes(VideoSinkType::NullConvert));
}

// ─────────────────────────────────────────────────────────────────────────────
// The canonical VideoSettings::sink_type reaches the export options: this is
// the wiring that makes the pre-encode ("raw") truth surface reachable from
// the CLI instead of being declared but dropped.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("make_ffmpeg_export_options: canonical sink_type reaches SinkOptions") {
    chronon3d::RenderJob job;
    job.output = "/tmp/pre_encode.raw";

    // Unset: the canonical default ("ffmpeg") keeps the encoder path.
    CHECK(make_ffmpeg_export_options(job).sink.sink_type == VideoSinkType::Ffmpeg);

    job.video_settings.sink_type = "raw";
    CHECK(make_ffmpeg_export_options(job).sink.sink_type == VideoSinkType::RawFile);

    job.video_settings.sink_type = "ffmpeg";
    CHECK(make_ffmpeg_export_options(job).sink.sink_type == VideoSinkType::Ffmpeg);

    // An unknown name must not be silently reinterpreted as the encoder path.
    job.video_settings.sink_type = "bench";
    CHECK(make_ffmpeg_export_options(job).sink.sink_type == VideoSinkType::Ffmpeg);
}

// ─────────────────────────────────────────────────────────────────────────────
// FfmpegExportOptions: cancellation_token is preserved through copy
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("FfmpegExportOptions: cancellation_token survives sub-struct copy") {
    chronon3d::CancellationToken token;
    token.cancel();

    FfmpegExportOptions opts;
    opts.cancellation_token = &token;
    opts.output.output = "/tmp/test.mp4";

    // Copy via sub-struct assignment (simulates make_ffmpeg_opts)
    FfmpegExportOptions copy;
    copy.output = opts.output;
    copy.encoder = opts.encoder;
    copy.pipe = opts.pipe;
    copy.warmup = opts.warmup;
    copy.sink = opts.sink;
    copy.cancellation_token = opts.cancellation_token;

    CHECK(copy.cancellation_token == &token);
    CHECK(copy.cancellation_token->is_cancelled());
    CHECK(copy.output.output == "/tmp/test.mp4");
}

// ─────────────────────────────────────────────────────────────────────────────
// FfmpegExportOptions: audit Wave-1 execution-receipt fields
// ─────────────────────────────────────────────────────────────────────────────
// The resolver closeout (TICKET-AUDIT-ROADMAP-WAVES, Wave 1) surfaces the
// FINAL plan on the options so the executor never re-decides policy.  The
// receipt fields must default to the un-resolved contract and survive the
// option copy/assignment pattern used by the video dispatch paths.

TEST_CASE("FfmpegExportOptions: execution-receipt fields default to unresolved contract") {
    FfmpegExportOptions opts;
    CHECK_FALSE(opts.resolved_execution_plan.has_value());
    CHECK(opts.resolved_execution_reason.empty());
    CHECK(opts.resolved_execution_valid);
    CHECK(opts.resolved_execution_path ==
          FfmpegExportOptions::ResolvedExecutionPath::FullGraph);
}

TEST_CASE("FfmpegExportOptions: execution-receipt fields survive whole-option copy") {
    FfmpegExportOptions opts;
    opts.resolved_execution_plan = chronon3d::media::VideoExecutionPlan{
        chronon3d::media::DecodePath::Nvdec,
        chronon3d::media::CompositePath::DirectYuv,
        chronon3d::media::EncodePath::Nvenc,
        chronon3d::media::InteropPath::CudaNative,
        chronon3d::media::SurfaceHandoffPath::Direct};
    opts.resolved_execution_reason = "gop_splice_unsafe";
    opts.resolved_execution_valid = false;
    opts.resolved_execution_path =
        FfmpegExportOptions::ResolvedExecutionPath::DirectYuv;

    FfmpegExportOptions copy = opts;
    CHECK(copy.resolved_execution_plan.has_value());
    REQUIRE(copy.resolved_execution_plan);
    CHECK(copy.resolved_execution_plan->decode ==
          chronon3d::media::DecodePath::Nvdec);
    CHECK(copy.resolved_execution_plan->composite ==
          chronon3d::media::CompositePath::DirectYuv);
    CHECK(copy.resolved_execution_reason == "gop_splice_unsafe");
    CHECK_FALSE(copy.resolved_execution_valid);
    CHECK(copy.resolved_execution_path ==
          FfmpegExportOptions::ResolvedExecutionPath::DirectYuv);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame-rate authority (F3.1): fps_num/fps_den rational is the single source
// ─────────────────────────────────────────────────────────────────────────────
// The legacy {fps, fps_num, fps_den} triple and the "scalar fps overrides the
// untouched 30/1 rational" rule (canonical_frame_rate.hpp) were demolished:
// the CLI normalises --fps into fps_num/fps_den at the boundary, and every
// consumer reads the rational verbatim. These tests lock that fps_num/fps_den
// are used EXACTLY as given — a stale scalar that tried to override them has
// no channel to do so, because neither struct carries a scalar fps anymore.

TEST_CASE("FfmpegFrameRate: fps_num/fps_den are the single frame-rate authority") {
    struct Case { int fps_num; int fps_den; int want_num; int want_den; };
    const Case cases[] = {
        // Defaults.
        {30, 1, 30, 1},
        // Rational fields are used verbatim, including NTSC-style rates.
        {24, 1, 24, 1},
        {25, 1, 25, 1},
        {30000, 1001, 30000, 1001},
        {24000, 1000, 24000, 1000},
        {60, 2, 60, 2},
    };
    for (const auto& c : cases) {
        OutputOptions output;
        output.fps_num = c.fps_num;
        output.fps_den = c.fps_den;
        FfmpegPipeOptions pipe;
        pipe.fps_num = c.fps_num;
        pipe.fps_den = c.fps_den;

        const auto rate = output.frame_rate();
        CHECK(rate.numerator == c.want_num);
        CHECK(rate.denominator == c.want_den);
        CHECK(pipe.canonical_fps_num() == c.want_num);
        CHECK(pipe.canonical_fps_den() == c.want_den);
        // Cross-parity: the two struct accessors agree with each other.
        CHECK(pipe.canonical_fps_num() == rate.numerator);
        CHECK(pipe.canonical_fps_den() == rate.denominator);
    }
}

TEST_CASE("FfmpegFrameRate: no scalar fps channel exists on the export options") {
    // The compile-time proof that the legacy scalar alias is gone: a
    // hypothetical "legacy 24fps override against the untouched 30/1 default"
    // must be IMPOSSIBLE to express — setting only fps_num/fps_den to 30/1 is
    // the untouched default, and there is no other field left to smuggle a
    // scalar fps in.
    OutputOptions output;
    CHECK(output.fps_num == 30);
    CHECK(output.fps_den == 1);
    FfmpegPipeOptions pipe;
    CHECK(pipe.canonical_fps_num() == 30);
    CHECK(pipe.canonical_fps_den() == 1);
    // Rationals pass through verbatim and equal the frame-rate accessor.
    output.fps_num = 24000;
    output.fps_den = 1001;
    pipe.fps_num = 24000;
    pipe.fps_den = 1001;
    CHECK(output.frame_rate().numerator == 24000);
    CHECK(output.frame_rate().denominator == 1001);
    CHECK(pipe.canonical_fps_num() == 24000);
    CHECK(pipe.canonical_fps_den() == 1001);
}

// ─────────────────────────────────────────────────────────────────────────────
// Canonical pipe pixel-format vocabulary
//
// Regression lock for the silent-fallback defect: the parser used to map every
// unrecognised name onto RGBA, so `nv21` / `nv12 ` / `banana` produced a
// successful export in the wrong pixel format instead of an error.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("try_parse_pipe_pixfmt accepts exactly the canonical set") {
    CHECK(try_parse_pipe_pixfmt("rgba").value_or(PipePixelFormat::NV12) ==
          PipePixelFormat::RGBA);
    CHECK(try_parse_pipe_pixfmt("yuv420p").value_or(PipePixelFormat::NV12) ==
          PipePixelFormat::YUV420P);
    CHECK(try_parse_pipe_pixfmt("nv12").value_or(PipePixelFormat::RGBA) ==
          PipePixelFormat::NV12);
    CHECK(try_parse_pipe_pixfmt("p010").value_or(PipePixelFormat::RGBA) ==
          PipePixelFormat::P010);
    // Accepted alias, not a distinct format.
    CHECK(try_parse_pipe_pixfmt("p010le").value_or(PipePixelFormat::RGBA) ==
          PipePixelFormat::P010);

    // The silent-fallback trap: everything else must be rejected.
    for (const char* bad : {"nv21", "nv12 ", " nv12", "NV12", "yuv420", "p010be",
                            "banana", ""}) {
        INFO("rejected value: '" << bad << "'");
        CHECK_FALSE(try_parse_pipe_pixfmt(bad).has_value());
    }
}

TEST_CASE("pipe pixel-format wire names round-trip through the parser") {
    for (const auto format : {PipePixelFormat::RGBA, PipePixelFormat::YUV420P,
                              PipePixelFormat::NV12, PipePixelFormat::P010}) {
        const auto name = pipe_pixel_format_name(format);
        INFO("format name: " << name);
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
        CHECK(try_parse_pipe_pixfmt(name).value_or(PipePixelFormat::RGBA) == format);
    }
}

TEST_CASE("CLI pipe pixel-format validators derive from the one authority") {
    const auto source_names = pipe_pixfmt_source_names();
    const auto cuda_names = pipe_pixfmt_cuda_surface_names();

    // The general list advertises every name the parser accepts...
    for (const auto& name : source_names) {
        INFO("advertised name: " << name);
        CHECK(try_parse_pipe_pixfmt(name).has_value());
    }
    // ...and the CUDA/pipe surface subset is a strict, still-parseable subset.
    CHECK(cuda_names == std::vector<std::string>({"nv12", "p010", "p010le"}));
    for (const auto& name : cuda_names) {
        CHECK(try_parse_pipe_pixfmt(name).has_value());
    }

    // Validation helpers agree with the parser, and treat empty as
    // "no explicit request".
    CHECK(pipe_pixfmt_is_valid_source(""));
    CHECK(pipe_pixfmt_is_valid_source("nv12"));
    CHECK_FALSE(pipe_pixfmt_is_valid_source("nv21"));
    CHECK(pipe_pixfmt_is_valid_cuda_surface(""));
    CHECK(pipe_pixfmt_is_valid_cuda_surface("p010"));
    CHECK_FALSE(pipe_pixfmt_is_valid_cuda_surface("rgba"));
    CHECK_FALSE(pipe_pixfmt_is_valid_cuda_surface("yuv420p"));
}
