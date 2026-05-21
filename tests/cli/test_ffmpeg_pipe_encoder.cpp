#include <doctest/doctest.h>
#include <apps/chronon3d_cli/utils/ffmpeg_pipe_encoder.hpp>

using namespace chronon3d::cli;

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
    opt.input_pix_fmt = "rgba";
    opt.output_pix_fmt = "yuv420p";

    const auto cmd = build_ffmpeg_raw_pipe_command(opt);

    CHECK(cmd.find("-pix_fmt rgba") != std::string::npos);
    CHECK(cmd.find("-s 1920x1080") != std::string::npos);
    CHECK(cmd.find("-r 30") != std::string::npos);
    CHECK(cmd.find("-c:v libx264") != std::string::npos);
    CHECK(cmd.find("-preset ultrafast") != std::string::npos);
}
