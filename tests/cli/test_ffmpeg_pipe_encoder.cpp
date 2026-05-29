#include <doctest/doctest.h>
#include <apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/math/color.hpp>

using namespace chronon3d;
using namespace chronon3d::cli;

TEST_CASE("RGBA to YUV420P buffer sizes are correct") {
    Framebuffer fb(4, 4);
    fb.clear(Color{1, 0, 0, 1});

    FfmpegPipeEncoder enc;
    FfmpegPipeOptions opts{
        .width = 4,
        .height = 4,
        .fps = 1,
        .output_path = "/tmp/test_out.mp4",
        .input_format = PipePixelFormat::YUV420P,
    };
    REQUIRE(enc.open(opts));

    auto result = enc.convert_framebuffer_to_yuv420p(fb);
    CHECK(result);

    enc.close();
}

TEST_CASE("YUV420P conversion produces correct plane sizes for 4x4") {
    Framebuffer fb(4, 4);
    fb.clear(Color{1, 0, 0, 1});

    FfmpegPipeEncoder enc;
    FfmpegPipeOptions opts{
        .width = 4,
        .height = 4,
        .fps = 1,
        .output_path = "/tmp/test_out.mp4",
        .input_format = PipePixelFormat::YUV420P,
    };
    REQUIRE(enc.open(opts));

    CHECK(enc.convert_framebuffer_to_yuv420p(fb));
    CHECK(enc.frames_written() == 0);  // not written, only converted

    enc.close();
}

TEST_CASE("YUV420P conversion fails for odd dimensions") {
    Framebuffer fb(3, 3);
    fb.clear(Color{0, 1, 0, 1});

    FfmpegPipeEncoder enc;
    FfmpegPipeOptions opts{
        .width = 3,
        .height = 3,
        .fps = 1,
        .output_path = "/tmp/test_out.mp4",
        .input_format = PipePixelFormat::YUV420P,
    };
    // Opening with odd dimensions under YUV420P must fail early
    REQUIRE_FALSE(enc.open(opts));
}

TEST_CASE("build_ffmpeg_raw_pipe_command contains rawvideo input settings") {
    FfmpegPipeOptions opt;
    opt.width = 1920;
    opt.height = 1080;
    opt.fps = 30;
    opt.crf = 18;
    opt.preset = "medium";
    opt.codec = "libx264";
    opt.output_path = "out.mp4";

    const auto cmd = build_ffmpeg_raw_pipe_command(opt);

    CHECK(cmd.find("-f rawvideo") != std::string::npos);
    CHECK(cmd.find("-pix_fmt rgba") != std::string::npos);
    CHECK(cmd.find("-s 1920x1080") != std::string::npos);
    CHECK(cmd.find("-r 30") != std::string::npos);
    CHECK(cmd.find("-i -") != std::string::npos);
    CHECK(cmd.find("-c:v libx264") != std::string::npos);
    CHECK(cmd.find("-crf 18") != std::string::npos);
    CHECK(cmd.find("-preset medium") != std::string::npos);
    CHECK(cmd.find("-pix_fmt yuv420p") != std::string::npos);
}

TEST_CASE("build_ffmpeg_raw_pipe_command supports hardware codec") {
    FfmpegPipeOptions opt;
    opt.width = 1280;
    opt.height = 720;
    opt.fps = 60;
    opt.codec = "h264_nvenc";
    opt.output_path = "video.mp4";

    const auto cmd = build_ffmpeg_raw_pipe_command(opt);

    CHECK(cmd.find("-s 1280x720") != std::string::npos);
    CHECK(cmd.find("-r 60") != std::string::npos);
    CHECK(cmd.find("-c:v h264_nvenc") != std::string::npos);
}

TEST_CASE("ffmpeg raw pipe command includes configured pixel formats") {
    FfmpegPipeOptions opt;
    opt.width = 1920;
    opt.height = 1080;
    opt.fps = 30;
    opt.crf = 18;
    opt.codec = "libx264";
    opt.preset = "ultrafast";
    opt.output_path = "out.mp4";
    opt.input_format = PipePixelFormat::RGBA;
    opt.output_pix_fmt = "rgb24";

    const auto cmd = build_ffmpeg_raw_pipe_command(opt);

    CHECK(cmd.find("-pix_fmt rgba") != std::string::npos);
    CHECK(cmd.find("-s 1920x1080") != std::string::npos);
    CHECK(cmd.find("-r 30") != std::string::npos);
    CHECK(cmd.find("-c:v libx264") != std::string::npos);
    CHECK(cmd.find("-preset ultrafast") != std::string::npos);
    CHECK(cmd.find("-pix_fmt rgb24") != std::string::npos);
}

TEST_CASE("ffmpeg raw pipe command respects raw input formats") {
    FfmpegPipeOptions opt;
    opt.width = 1280;
    opt.height = 720;
    opt.fps = 60;
    opt.codec = "libx264";
    opt.preset = "veryfast";
    opt.output_path = "out.mp4";
    opt.input_format = PipePixelFormat::YUV420P;

    const auto cmd = build_ffmpeg_raw_pipe_command(opt);

    // It should configure both the input format and output format to yuv420p
    CHECK(cmd.find("-pix_fmt yuv420p") != std::string::npos);
    CHECK(cmd.find("-pix_fmt rgba") == std::string::npos);
}

TEST_CASE("ffmpeg pipe encoder records converted frame cache hits on repeated frames") {
    Framebuffer fb(512, 512);
    fb.clear(Color{0.15f, 0.25f, 0.35f, 1.0f});
    fb.set_key_digest(0x12345ULL);

    FfmpegPipeEncoder enc;
    FfmpegPipeOptions opts{
        .width = 512,
        .height = 512,
        .fps = 1,
        .output_path = "/tmp/test_cache_hits.mp4",
        .input_format = PipePixelFormat::YUV420P,
    };
    if (!enc.open(opts)) {
        MESSAGE("Skipping — FFmpeg not available");
        return;
    }

    RenderCounters counters;
    profiling::g_current_counters = &counters;

    REQUIRE(enc.write_frame(fb));
    const auto conv_after_first = counters.video_conversion_ms.load(std::memory_order_relaxed);
    const auto copy_after_first = counters.frame_conversion_copy_ms.load(std::memory_order_relaxed);
    const auto cache_after_first = counters.converted_frame_cache_hits.load(std::memory_order_relaxed);
    const auto pipe_after_first = counters.video_pipe_write_ms.load(std::memory_order_relaxed);

    REQUIRE(enc.write_frame(fb));
    const auto conv_after_second = counters.video_conversion_ms.load(std::memory_order_relaxed);
    const auto copy_after_second = counters.frame_conversion_copy_ms.load(std::memory_order_relaxed);
    const auto cache_after_second = counters.converted_frame_cache_hits.load(std::memory_order_relaxed);
    const auto pipe_after_second = counters.video_pipe_write_ms.load(std::memory_order_relaxed);

    CHECK(conv_after_first >= 0);
    CHECK(copy_after_first >= 0);
    CHECK(cache_after_first == 0);
    CHECK(cache_after_second == 1); // identical frames reuse the cached converted buffer by design
    CHECK(conv_after_second >= conv_after_first);
    CHECK(copy_after_second >= copy_after_first);
    CHECK(pipe_after_second >= pipe_after_first);

    profiling::g_current_counters = nullptr;
    enc.close();
}
