// ---------------------------------------------------------------------------
// test_frame_rate_video_validation.cpp — P2-D: Video frame rate validation.
//
// Tests VideoSinkConfig validation and frame_buffer_size for various rates.
// Requires CHRONON3D_ENABLE_VIDEO (registered in media_tests.cmake).
// ---------------------------------------------------------------------------

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/video_config.hpp>
#include <chronon3d/media/video/video_frame.hpp>

#include <cstdint>
#include <string>

using namespace chronon3d::media::video;

namespace {

/// Build a VideoSinkConfig with the given frame rate, valid otherwise.
[[nodiscard]] VideoSinkConfig make_rate_config(int num, int den) {
    VideoSinkConfig cfg;
    cfg.stream.width  = 64;
    cfg.stream.height = 64;
    cfg.stream.frame_rate_num = num;
    cfg.stream.frame_rate_den = den;
    cfg.stream.submitted_format = PixelFormat::RGBA8;
    cfg.encoder.codec = VideoCodec::H264;
    cfg.encoder.crf   = 30;
    cfg.encoder.preset = "ultrafast";
    cfg.output.output_path = "/tmp/test_framerate.mp4";
    cfg.output.container = VideoContainer::Mp4;
    return cfg;
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════
//  Section G: VideoSinkConfig validation accepts all standard rates
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("VideoSinkConfig: 23.976 fps (24000/1001) passes validation") {
    auto cfg = make_rate_config(24000, 1001);
    auto result = validate_video_sink_config(cfg);
    CHECK(result.valid);
}

TEST_CASE("VideoSinkConfig: 24 fps passes validation") {
    auto cfg = make_rate_config(24, 1);
    CHECK(validate_video_sink_config(cfg).valid);
}

TEST_CASE("VideoSinkConfig: 25 fps passes validation") {
    auto cfg = make_rate_config(25, 1);
    CHECK(validate_video_sink_config(cfg).valid);
}

TEST_CASE("VideoSinkConfig: 29.97 fps (30000/1001) passes validation") {
    auto cfg = make_rate_config(30000, 1001);
    CHECK(validate_video_sink_config(cfg).valid);
}

TEST_CASE("VideoSinkConfig: 30 fps passes validation") {
    auto cfg = make_rate_config(30, 1);
    CHECK(validate_video_sink_config(cfg).valid);
}

TEST_CASE("VideoSinkConfig: 59.94 fps (60000/1001) passes validation") {
    auto cfg = make_rate_config(60000, 1001);
    CHECK(validate_video_sink_config(cfg).valid);
}

TEST_CASE("VideoSinkConfig: 60 fps passes validation") {
    auto cfg = make_rate_config(60, 1);
    CHECK(validate_video_sink_config(cfg).valid);
}

TEST_CASE("VideoSinkConfig: 120 fps passes validation") {
    auto cfg = make_rate_config(120, 1);
    CHECK(validate_video_sink_config(cfg).valid);
}

TEST_CASE("VideoSinkConfig: 1 fps passes validation") {
    auto cfg = make_rate_config(1, 1);
    CHECK(validate_video_sink_config(cfg).valid);
}

TEST_CASE("VideoSinkConfig: frame_rate_num == 0 fails validation") {
    auto cfg = make_rate_config(0, 1);
    auto result = validate_video_sink_config(cfg);
    CHECK_FALSE(result.valid);
    CHECK(result.error_message.find("frame_rate_num") != std::string::npos);
}

TEST_CASE("VideoSinkConfig: frame_rate_den == 0 fails validation") {
    auto cfg = make_rate_config(30, 0);
    auto result = validate_video_sink_config(cfg);
    CHECK_FALSE(result.valid);
    CHECK(result.error_message.find("frame_rate_den") != std::string::npos);
}

TEST_CASE("VideoSinkConfig: negative frame_rate_num fails validation") {
    auto cfg = make_rate_config(-30, 1);
    auto result = validate_video_sink_config(cfg);
    CHECK_FALSE(result.valid);
}

TEST_CASE("VideoSinkConfig: negative frame_rate_den fails validation") {
    auto cfg = make_rate_config(30, -1);
    auto result = validate_video_sink_config(cfg);
    CHECK_FALSE(result.valid);
}

// ═════════════════════════════════════════════════════════════════════════
//  Section I: frame_buffer_size is fps-independent (sanity)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("frame_buffer_size: depends only on geometry, not fps") {
    constexpr int W = 1920, H = 1080;
    uint64_t rgba_size = frame_buffer_size(PixelFormat::RGBA8, W, H);
    CHECK(rgba_size == static_cast<uint64_t>(W) * H * 4);

    uint64_t yuv_size = frame_buffer_size(PixelFormat::YUV420P, W, H);
    uint64_t expected_yuv = static_cast<uint64_t>(W) * H
                          + static_cast<uint64_t>(W / 2) * (H / 2) * 2;
    CHECK(yuv_size == expected_yuv);

    // Verify determinism: calling twice yields same result.
    CHECK(frame_buffer_size(PixelFormat::YUV420P, W, H) == yuv_size);
}
