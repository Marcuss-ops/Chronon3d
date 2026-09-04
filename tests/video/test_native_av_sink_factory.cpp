#include <chronon3d/media/video/video_config.hpp>
#include <chronon3d/media/video/video_sink_factory.hpp>

#include <doctest/doctest.h>

using namespace chronon3d::media::video;

TEST_CASE("VideoSinkFactory: native FFmpeg build selects in-process compressed sink") {
    VideoSinkConfig config;
    config.stream.width = 64;
    config.stream.height = 64;
    config.stream.submitted_format = PixelFormat::RGBA8;
    config.encoder.codec = VideoCodec::H264;
    config.output.container = VideoContainer::Mp4;
    config.output.output_path = "native-factory-test.mp4";

    auto sink = create_video_sink(config);
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
    CHECK(sink->name() == "raw-file");
}
