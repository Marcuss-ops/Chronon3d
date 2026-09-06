#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands/video/common/video_export_common.hpp>

using namespace chronon3d::cli;

// ─────────────────────────────────────────────────────────────────────────────
// FfmpegExportOptions: sub-struct defaults match expected values
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("FfmpegExportOptions: default-constructed sub-structs have correct defaults") {
    FfmpegExportOptions opts;

    // OutputOptions defaults
    CHECK(opts.output.output.empty());
    CHECK(opts.output.frames_dir_name.empty());
    CHECK(opts.output.fps == 30);

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
    out.fps = 60;

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
    CHECK(opts.output.fps == 60);
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
    CHECK(o.fps == 30);
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
// Frame-rate canonicalisation parity (canonical_frame_rate.hpp)
// ─────────────────────────────────────────────────────────────────────────────
// The {fps, fps_num, fps_den} triple is carried by both OutputOptions and
// FfmpegPipeOptions, and the "which field wins" rule must be identical for the
// two accessors (single-source canonical_frame_rate()). Lock the rule and the
// parity so a future fix to one side cannot silently desynchronise the other.

TEST_CASE("FfmpegFrameRate: canonical_frame_rate is the single source of truth") {
    const auto canonical = chronon3d::cli::canonical_frame_rate;
    struct Case { int fps; int fps_num; int fps_den; int want_num; int want_den; };
    const Case cases[] = {
        // Defaults: untouched rational sentinel 30/1, untouched legacy fps=30.
        {30, 30, 1, 30, 1},
        // Legacy integer fps overrides the untouched 30/1 default.
        {24, 30, 1, 24, 1},
        {25, 30, 1, 25, 1},
        // Non-default rational fields are authoritative even when legacy fps
        // differs (NTSC 30000/1001 and 24/1.001-style rational).
        {25, 30000, 1001, 30000, 1001},
        {30, 24000, 1000, 24000, 1000},
        // Sentinel edge: fps_num==30 && fps_den==1 && fps==30 is
        // indistinguishable from an untouched default; result is 30/1.
        {30, 30, 1, 30, 1},
    };
    for (const auto& c : cases) {
        const auto got = canonical(c.fps, c.fps_num, c.fps_den);
        CHECK(got.num == c.want_num);
        CHECK(got.den == c.want_den);
    }
}

TEST_CASE("FfmpegFrameRate: OutputOptions and FfmpegPipeOptions agree") {
    // Parity lock: for every representative triple, OutputOptions::frame_rate()
    // and FfmpegPipeOptions::canonical_fps_num()/den() must resolve to the same
    // rational — including the legacy branch and the mixed rational cases.
    struct Case { int fps; int fps_num; int fps_den; };
    const Case cases[] = {
        {30, 30, 1}, {24, 30, 1}, {25, 30, 1}, {60, 60, 1},
        {25, 30000, 1001}, {30, 24000, 1000},
        // The case the two inlined heuristics historically disagreed on:
        // legacy fps differs AND fps_den != 1 (legacy must not apply).
        {24, 30, 2}, {25, 60, 2},
    };
    for (const auto& c : cases) {
        OutputOptions output;
        output.fps = c.fps;
        output.fps_num = c.fps_num;
        output.fps_den = c.fps_den;

        FfmpegPipeOptions pipe;
        pipe.fps = c.fps;
        pipe.fps_num = c.fps_num;
        pipe.fps_den = c.fps_den;

        const auto canonical = chronon3d::cli::canonical_frame_rate(
            c.fps, c.fps_num, c.fps_den);
        const auto rate = output.frame_rate();
        CHECK(rate.numerator == canonical.num);
        CHECK(rate.denominator == canonical.den);
        CHECK(pipe.canonical_fps_num() == canonical.num);
        CHECK(pipe.canonical_fps_den() == canonical.den);
        // Cross-parity: the two struct accessors agree with each other.
        CHECK(pipe.canonical_fps_num() == rate.numerator);
        CHECK(pipe.canonical_fps_den() == rate.denominator);
    }
}
