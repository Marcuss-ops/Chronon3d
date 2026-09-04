#include <chronon3d/media/video/video_config.hpp>
#include <chronon3d/media/video/video_sink_factory.hpp>

#include <doctest/doctest.h>

using namespace chronon3d::media::video;

namespace {

VideoSinkConfig compressed_config(VideoCodec codec, const char* output_path) {
    VideoSinkConfig config;
    config.stream.width = 64;
    config.stream.height = 64;
    config.stream.submitted_format = PixelFormat::RGBA8;
    config.encoder.codec = codec;
    config.output.container = VideoContainer::Mp4;
    config.output.output_path = output_path;
    return config;
}

} // namespace

TEST_CASE("VideoSinkFactory: native FFmpeg build routes H264 in-process") {
    auto sink = create_video_sink(
        compressed_config(VideoCodec::H264, "native-factory-h264.mp4"));
    REQUIRE(sink != nullptr);
    CHECK(sink->name() == "native-av");
}

TEST_CASE("VideoSinkFactory: native FFmpeg build routes H265 in-process") {
    auto sink = create_video_sink(
        compressed_config(VideoCodec::H265, "native-factory-h265.mp4"));
    REQUIRE(sink != nullptr);
    CHECK(sink->name() == "native-av");
}

TEST_CASE("VideoSinkFactory: native FFmpeg build keeps raw output on RawVideoSink") {
    VideoSinkConfig config;
    config.stream.width = 64;
    config.stream.height = 64;
    config.stream.submitted_format = PixelFormat::RGBA8;
    config.encoder.codec = VideoCodec::Uncompressed;
    config.output.container = VideoContainer::Raw;
    config.output.output_path = "native-factory-test.raw";

    auto sink = create_video_sink(config);
    REQUIRE(sink != nullptr);
    CHECK(sink->name() == "raw-video-sink");
}
