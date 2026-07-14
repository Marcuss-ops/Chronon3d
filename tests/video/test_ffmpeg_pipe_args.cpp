// ============================================================================
// tests/video/test_ffmpeg_pipe_args.cpp — Per-TU unit tests for
// FfmpegPipeSinkInternal::build_argv (Phase-3 of TICKET-FFMPEG-PIPE-SINK-SPLIT).
//
// Goal: exercise the argv construction PASS without spawning an ffmpeg
// subprocess.  FfmpegPipeSinkInternal::build_argv is a pure data → data
// function (no FfmpegPipeSink instance needed); we instantiate a
// VideoSinkConfig + invoke the static method directly.
//
// Tier: UNIT (per-TU; no subprocess / no network — verifiable on VPS).
//
// Cross-link: TICKET-FFMPEG-PIPE-SINK-SPLIT Phase-3 forward-point (open since
// 2026-07-14; this chore closes it).
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/media/video/video_config.hpp>

#include "src/media/video/ffmpeg_pipe_sink_internal.hpp"  // for FfmpegPipeSinkInternal::build_argv

#include <filesystem>
#include <string>
#include <vector>

namespace chronon3d::media::video {
namespace {

// ── Helper: build a VideoSinkConfig with sensible defaults ────────────
//
// centralizes the boilerplate so each SUBCASE focuses on the
// delta-being-tested (codec/container/overwrite/etc.).
VideoSinkConfig make_default_config(VideoCodec codec = VideoCodec::H264,
                                    VideoContainer container = VideoContainer::Mp4,
                                    PixelFormat submitted_format = PixelFormat::RGBA8,
                                    PixelFormat encoded_format = PixelFormat::RGBA8,
                                    std::filesystem::path output_path =
                                        std::filesystem::path("/tmp/chronon3d_test.mp4")) {
    VideoSinkConfig config;
    config.stream.width = 1920;
    config.stream.height = 1080;
    config.stream.frame_rate_num = 30;
    config.stream.frame_rate_den = 1;
    config.stream.submitted_format = submitted_format;
    config.encoder.codec = codec;
    config.encoder.encoded_pixel_format = encoded_format;
    config.encoder.crf = 23;
    config.output.container = container;
    config.output.overwrite = true;
    config.output.output_path = output_path;
    return config;
}

bool contains_arg(const std::vector<std::string>& argv, const std::string& needle) {
    for (const auto& arg : argv) {
        if (arg == needle) return true;
    }
    return false;
}

} // anonymous namespace

TEST_CASE("build_argv: standard config (RGBA8 + H264 + MP4) emits canonical argv") {
    const auto config = make_default_config();
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    REQUIRE(!argv.empty());
    CHECK(argv[0] == "ffmpeg");
    CHECK(argv[1] == "-hide_banner");
    CHECK(argv[2] == "-loglevel");
    CHECK(argv[3] == "error");
    CHECK(contains_arg(argv, "-y"));
    CHECK(contains_arg(argv, "-n") == false);
    CHECK(contains_arg(argv, "rawvideo"));
    CHECK(contains_arg(argv, "-f"));
    CHECK(contains_arg(argv, "-pix_fmt"));
    CHECK(contains_arg(argv, "rgba"));        // RGBA8 → ffmpeg "rgba"
    CHECK(contains_arg(argv, "-s"));
    CHECK(contains_arg(argv, "1920x1080"));  // standard WxH
    CHECK(contains_arg(argv, "-r"));
    CHECK(contains_arg(argv, "30/1"));       // standard frame-rate ratio
    CHECK(contains_arg(argv, "-i"));
    CHECK(contains_arg(argv, "-"));          // stdin input
    CHECK(contains_arg(argv, "-an"));        // no audio
    CHECK(contains_arg(argv, "-c:v"));
    CHECK(contains_arg(argv, "libx264"));   // H264 → libx264
    CHECK(contains_arg(argv, "-crf"));
    CHECK(contains_arg(argv, "23"));
    CHECK(contains_arg(argv, "/tmp/chronon3d_test.mp4"));  // output path
}

TEST_CASE("build_argv: overwrite=false emits -n, suppresses -y (mutual exclusion)") {
    auto config = make_default_config();
    config.output.overwrite = false;

    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK(contains_arg(argv, "-n"));
    CHECK_FALSE(contains_arg(argv, "-y"));
}

TEST_CASE("build_argv: H265 codec → libx265 mapping (per codec_to_string table)") {
    auto config = make_default_config(VideoCodec::H265);
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK(contains_arg(argv, "libx265"));
    CHECK_FALSE(contains_arg(argv, "libx264"));
}

TEST_CASE("build_argv: VP9 codec + WebM container (per codec + container_extension tables)") {
    auto config = make_default_config(VideoCodec::VP9, VideoContainer::WebM,
                                     PixelFormat::RGBA8, PixelFormat::RGBA8,
                                     std::filesystem::path("/tmp/chronon3d_vp9")); // no dot
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK(contains_arg(argv, "libvpx-vp9"));
    CHECK(contains_arg(argv, ".webm"));
}

TEST_CASE("build_argv: AV1 codec → libaom-av1 mapping") {
    auto config = make_default_config(VideoCodec::AV1, VideoContainer::Mp4);
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK(contains_arg(argv, "libaom-av1"));
}

TEST_CASE("build_argv: YUV420P encoded pixel format adds BT.709 color metadata block") {
    auto config = make_default_config(VideoCodec::H264, VideoContainer::Mp4,
                                     PixelFormat::YUV420P, PixelFormat::YUV420P);
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK(contains_arg(argv, "-colorspace"));
    CHECK(contains_arg(argv, "bt709"));
    CHECK(contains_arg(argv, "-color_primaries"));
    CHECK(contains_arg(argv, "-color_trc"));
    CHECK(contains_arg(argv, "-color_range"));
    CHECK(contains_arg(argv, "tv"));
    CHECK(contains_arg(argv, "yuv420p"));  // encoded pixel format
}

TEST_CASE("build_argv: NV12 encoded pixel format adds color metadata (same BT.709 path)") {
    auto config = make_default_config(VideoCodec::H264, VideoContainer::Mp4,
                                     PixelFormat::NV12, PixelFormat::NV12);
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK(contains_arg(argv, "bt709"));
    CHECK(contains_arg(argv, "nv12"));
}

TEST_CASE("build_argv: RGBA8 encoded pixel format suppresses color metadata block") {
    auto config = make_default_config(VideoCodec::H264, VideoContainer::Mp4,
                                     PixelFormat::RGBA8, PixelFormat::RGBA8);
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK_FALSE(contains_arg(argv, "-colorspace"));
    CHECK_FALSE(contains_arg(argv, "bt709"));
}

TEST_CASE("build_argv: output_path without extension → appended container extension") {
    auto config = make_default_config(VideoCodec::H264, VideoContainer::Mp4,
                                     PixelFormat::RGBA8, PixelFormat::RGBA8,
                                     std::filesystem::path("/tmp/no_extension_at_all"));
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    REQUIRE_FALSE(argv.empty());
    CHECK(argv.back() == "/tmp/no_extension_at_all.mp4");
}

TEST_CASE("build_argv: output_path with dot → extension NOT appended") {
    auto config = make_default_config(VideoCodec::H264, VideoContainer::Mp4,
                                     PixelFormat::RGBA8, PixelFormat::RGBA8,
                                     std::filesystem::path("/tmp/already.mp4"));
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    REQUIRE_FALSE(argv.empty());
    CHECK(argv.back() == "/tmp/already.mp4");
}

TEST_CASE("build_argv: 24000/1001 frame-rate encodes as N/M (not normalized)") {
    auto config = make_default_config();
    config.stream.frame_rate_num = 24000;
    config.stream.frame_rate_den = 1001;

    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK(contains_arg(argv, "24000/1001"));
}

TEST_CASE("build_argv: MP4 container emits frag_keyframe+empty_moov movflags (piped-MP4 contract)") {
    auto config = make_default_config(VideoCodec::H264, VideoContainer::Mp4);
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK(contains_arg(argv, "-movflags"));
    CHECK(contains_arg(argv, "frag_keyframe+empty_moov"));
}

TEST_CASE("build_argv: MKV container suppresses movflags (only MP4 is piped-optimized)") {
    auto config = make_default_config(VideoCodec::H264, VideoContainer::Mkv);
    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK_FALSE(contains_arg(argv, "-movflags"));
    CHECK(contains_arg(argv, ".mkv"));
}

TEST_CASE("build_argv: crf < 0 suppresses the -crf arg (passes through ffmpeg default)") {
    auto config = make_default_config();
    config.encoder.crf = -1;

    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK_FALSE(contains_arg(argv, "-crf"));
}

TEST_CASE("build_argv: bitrate > 0 emits -b:v N kbps") {
    auto config = make_default_config();
    config.encoder.bitrate = 5'000'000;

    const auto argv = FfmpegPipeSinkInternal::build_argv(config);

    CHECK(contains_arg(argv, "-b:v"));
    CHECK(contains_arg(argv, "5000000"));
}

} // namespace chronon3d::media::video
