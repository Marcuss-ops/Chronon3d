#include <doctest/doctest.h>
#include <apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp>
#include <chronon3d/core/framebuffer.hpp>
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
        .output_path = "/dev/null",
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
        .output_path = "/dev/null",
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
        .output_path = "/dev/null",
        .input_format = PipePixelFormat::YUV420P,
    };
    REQUIRE(enc.open(opts));

    // YUV420P requires even dimensions
    CHECK_FALSE(enc.convert_framebuffer_to_yuv420p(fb));

    enc.close();
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
}

TEST_CASE("ffmpeg raw pipe command ignores legacy raw input formats") {
    FfmpegPipeOptions opt;
    opt.width = 1280;
    opt.height = 720;
    opt.fps = 60;
    opt.codec = "libx264";
    opt.preset = "veryfast";
    opt.output_path = "out.mp4";
    opt.input_format = PipePixelFormat::YUV420P;

    const auto cmd = build_ffmpeg_raw_pipe_command(opt);

    CHECK(cmd.find("-pix_fmt rgba") != std::string::npos);
    CHECK(cmd.find("-pix_fmt rgb24") != std::string::npos);
}
