#include <chronon3d/media/video/video_config.hpp>
#include <chronon3d/media/video/video_sink_factory.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

using namespace chronon3d::media::video;

namespace {

void certify_native_lifecycle(VideoCodec codec, const char* output_name) {
    const std::filesystem::path output_path = output_name;
    std::error_code ec;
    std::filesystem::remove(output_path, ec);

    VideoSinkConfig config;
    config.stream.width = 64;
    config.stream.height = 64;
    config.stream.frame_rate_num = 24;
    config.stream.frame_rate_den = 1;
    config.stream.submitted_format = PixelFormat::RGBA8;
    config.encoder.codec = codec;
    config.encoder.encoded_pixel_format = PixelFormat::YUV420P;
    config.output.container = VideoContainer::Mp4;
    config.output.output_path = output_path;
    config.output.overwrite = true;

    auto sink = create_video_sink(config);
    REQUIRE(sink != nullptr);
    REQUIRE(sink->name() == "native-av");
    REQUIRE(sink->open(config));
    CHECK(sink->state() == VideoSinkState::Open);

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(config.stream.width) *
        static_cast<std::size_t>(config.stream.height) * 4U,
        0x40U);

    const VideoFrameView frame{
        .data = pixels.data(),
        .stride_bytes = static_cast<std::size_t>(config.stream.width) * 4U,
        .width = config.stream.width,
        .height = config.stream.height,
        .pixel_format = PixelFormat::RGBA8,
        .pts = 0,
    };

    REQUIRE(sink->submit(frame));
    CHECK(sink->frames_submitted() == 1U);
    REQUIRE(sink->flush());
    REQUIRE(sink->close());
    CHECK(sink->state() == VideoSinkState::Closed);
    CHECK(std::filesystem::exists(output_path));

    std::filesystem::remove(output_path, ec);
}

} // namespace

TEST_CASE("NativeAvSink: H264 open submit flush close") {
    certify_native_lifecycle(VideoCodec::H264, "native-av-lifecycle-h264.mp4");
}

TEST_CASE("NativeAvSink: H265 open submit flush close") {
    certify_native_lifecycle(VideoCodec::H265, "native-av-lifecycle-h265.mp4");
}
