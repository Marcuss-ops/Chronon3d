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

TEST_CASE("NativeAvSink: codec-id fallback never silently selects a hardware encoder") {
    using namespace chronon3d::media::video;

    // Hardware names are recognized across vendors (NVENC/QSV/AMF/VideoToolbox).
    CHECK(is_hardware_encoder_name("h264_nvenc"));
    CHECK(is_hardware_encoder_name("hevc_qsv"));
    CHECK(is_hardware_encoder_name("h264_amf"));
    CHECK(is_hardware_encoder_name("h264_videotoolbox"));
    CHECK_FALSE(is_hardware_encoder_name("libx264"));
    CHECK_FALSE(is_hardware_encoder_name("libx264rgb"));
    CHECK_FALSE(is_hardware_encoder_name("libvpx-vp9"));

    // Same name as preferred: use it.
    CHECK(encoder_fallback_policy("libx264", "libx264") ==
          EncoderFallbackPolicy::UseChosen);
    // Software alternative for the same codec id: acceptable substitution.
    CHECK(encoder_fallback_policy("libx264", "libx264rgb") ==
          EncoderFallbackPolicy::UseChosenSoftware);
    // The only fallback is a hardware encoder the configuration did not
    // request: fail closed (the historical silent substitution fed x264
    // options to h264_nvenc and failed at avcodec_open2 with a misleading
    // error).
    CHECK(encoder_fallback_policy("libx264", "h264_nvenc") ==
          EncoderFallbackPolicy::FailClosed);
    // No usable fallback is fail-closed too.
    CHECK(encoder_fallback_policy("libx264", nullptr) ==
          EncoderFallbackPolicy::FailClosed);
    CHECK(encoder_fallback_policy("libx264", "") ==
          EncoderFallbackPolicy::FailClosed);
}
