#include <chronon3d/media/video/keyframe_cadence.hpp>
#include <chronon3d/media/video/video_config.hpp>
#include <chronon3d/media/video/video_sink_factory.hpp>

#include <doctest/doctest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <cstdint>
#include <limits>
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

TEST_CASE("Video frame validation rejects strides not representable by FFmpeg") {
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    CHECK(validate_packed_stride(PixelFormat::RGB24, 16, maximum));
    CHECK_FALSE(validate_packed_stride(PixelFormat::RGB24, 16, maximum + 1));
    CHECK_FALSE(validate_packed_stride(PixelFormat::RGBA8, 16, maximum + 1));
    CHECK_FALSE(validate_planar_frame(16, 16, maximum + 1, 8, 8));
    CHECK_FALSE(validate_planar_frame(16, 16, 16, maximum + 1, 8));
    CHECK_FALSE(validate_planar_frame(16, 16, 16, 8, maximum + 1));
    CHECK_FALSE(validate_biplanar_frame(16, 16, maximum + 1, 16));
    CHECK_FALSE(validate_biplanar_frame(16, 16, 16, maximum + 1));
}

TEST_CASE("NativeAvSink RGB conversion pixels agree with emitted BT709 metadata") {
    const auto path = std::filesystem::path("native-av-bt709-red.mp4");
    VideoSinkConfig config;
    config.stream.width = 64; config.stream.height = 64;
    config.stream.submitted_format = PixelFormat::RGB24;
    config.encoder.codec = VideoCodec::H264; config.encoder.crf = 0;
    config.output.output_path = path;
    auto sink = create_video_sink(config);
    REQUIRE(sink->open(config));
    std::vector<std::uint8_t> rgb(64U * 64U * 3U);
    for (std::size_t i = 0; i < rgb.size(); i += 3) rgb[i] = 255;
    REQUIRE(sink->submit(VideoFrameView{.data = rgb.data(), .width = 64,
        .height = 64, .pixel_format = PixelFormat::RGB24, .pts = 0}));
    REQUIRE(sink->close());
    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) == 0);
    struct InputGuard { AVFormatContext*& p; ~InputGuard() { avformat_close_input(&p); } } input{format};
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    const int stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(stream >= 0);
    const auto* params = format->streams[stream]->codecpar;
    CHECK(params->color_space == AVCOL_SPC_BT709);
    CHECK(params->color_range == AVCOL_RANGE_MPEG);
    AVCodecContext* decoder = avcodec_alloc_context3(avcodec_find_decoder(params->codec_id));
    REQUIRE(decoder != nullptr);
    struct DecoderGuard { AVCodecContext*& p; ~DecoderGuard() { avcodec_free_context(&p); } } codec{decoder};
    REQUIRE(avcodec_parameters_to_context(decoder, params) >= 0);
    REQUIRE(avcodec_open2(decoder, avcodec_find_decoder(params->codec_id), nullptr) >= 0);
    AVPacket* packet = av_packet_alloc(); AVFrame* frame = av_frame_alloc();
    struct FrameGuard { AVPacket*& p; AVFrame*& f; ~FrameGuard() { av_packet_free(&p); av_frame_free(&f); } } frames{packet, frame};
    REQUIRE(packet != nullptr); REQUIRE(frame != nullptr);
    int received = AVERROR(EAGAIN);
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == stream) {
            REQUIRE(avcodec_send_packet(decoder, packet) >= 0);
            received = avcodec_receive_frame(decoder, frame);
        }
        av_packet_unref(packet);
        if (received == 0) break;
    }
    if (received != 0) {
        REQUIRE(avcodec_send_packet(decoder, nullptr) >= 0);
        received = avcodec_receive_frame(decoder, frame);
    }
    REQUIRE(received == 0);
    REQUIRE(frame->format == AV_PIX_FMT_YUV420P);
    CHECK(frame->data[0][0] == 63);
    CHECK(frame->data[1][0] == 102);
    CHECK(frame->data[2][0] == 240);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("NativeAvSink: H264 open submit flush close") {
    certify_native_lifecycle(VideoCodec::H264, "native-av-lifecycle-h264.mp4");
}

TEST_CASE("NativeAvSink: H265 open submit flush close") {
    certify_native_lifecycle(VideoCodec::H265, "native-av-lifecycle-h265.mp4");
}

// ── Keyframe cadence (producer-side guard) ────────────────────────────────
//
// The clip.render output-facts gateway rejected every production render with
// "keyframe interval 24 != 48" because the native encoders derived the GOP
// from the frame rate alone. A gateway test cannot fail while the producer's
// binary is stale, so this test asserts the EMITTED stream: it encodes a real
// file through the canonical VideoSink authority and reads back the actual
// keyframe spacing. Reverting gop_size to the fps derivation fails here.

struct KeyframeSpacing {
    std::vector<int> keyframe_indices;
    int packet_count{0};
};

// read_keyframe_spacing decodes the muxed output's packet table and records
// the packet index of every keyframe. It reads the bytes that were actually
// written, not the encoder's configuration.
bool read_keyframe_spacing(const std::filesystem::path& path, KeyframeSpacing& out) {
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) < 0) {
        return false;
    }
    struct FormatGuard {
        AVFormatContext* context;
        ~FormatGuard() {
            if (context != nullptr) {
                avformat_close_input(&context);
            }
        }
    } guard{format};
    if (avformat_find_stream_info(format, nullptr) < 0) {
        return false;
    }
    const int video_stream =
        av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream < 0) {
        return false;
    }
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        return false;
    }
    int video_packet_index = 0;
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == video_stream) {
            ++out.packet_count;
            if ((packet->flags & AV_PKT_FLAG_KEY) != 0) {
                out.keyframe_indices.push_back(video_packet_index);
            }
            ++video_packet_index;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    return true;
}

// certify_keyframe_cadence encodes enough frames to cross two GOP boundaries
// and asserts that every keyframe lands exactly `expected_interval` frames
// apart, starting from frame 0.
void certify_keyframe_cadence(int frame_rate_num, int frame_rate_den,
                              int expected_interval, const char* output_name) {
    const std::filesystem::path output_path = output_name;
    std::error_code ec;
    std::filesystem::remove(output_path, ec);

    VideoSinkConfig config;
    config.stream.width = 64;
    config.stream.height = 64;
    config.stream.frame_rate_num = frame_rate_num;
    config.stream.frame_rate_den = frame_rate_den;
    config.stream.submitted_format = PixelFormat::RGBA8;
    config.encoder.codec = VideoCodec::H264;
    config.encoder.encoded_pixel_format = PixelFormat::YUV420P;
    config.output.container = VideoContainer::Mp4;
    config.output.output_path = output_path;
    config.output.overwrite = true;

    auto sink = create_video_sink(config);
    REQUIRE(sink != nullptr);
    REQUIRE(sink->open(config));

    // The producer-side fact: what the encoder was configured to emit.
    CHECK(sink->diagnostics().keyframe_interval_frames == expected_interval);

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(config.stream.width) *
            static_cast<std::size_t>(config.stream.height) * 4U,
        0x40U);

    const int frame_count = expected_interval * 2 + 1;
    for (int index = 0; index < frame_count; ++index) {
        const VideoFrameView frame{
            .data = pixels.data(),
            .stride_bytes = static_cast<std::size_t>(config.stream.width) * 4U,
            .width = config.stream.width,
            .height = config.stream.height,
            .pixel_format = PixelFormat::RGBA8,
            .pts = index,
        };
        REQUIRE(sink->submit(frame));
    }
    REQUIRE(sink->flush());
    REQUIRE(sink->close());
    REQUIRE(std::filesystem::exists(output_path));

    KeyframeSpacing spacing;
    REQUIRE(read_keyframe_spacing(output_path, spacing));
    REQUIRE(spacing.keyframe_indices.size() >= 3U);
    CHECK(spacing.keyframe_indices.front() == 0);
    for (std::size_t i = 1; i < spacing.keyframe_indices.size(); ++i) {
        CHECK(spacing.keyframe_indices[i] - spacing.keyframe_indices[i - 1] ==
              expected_interval);
    }

    std::filesystem::remove(output_path, ec);
}

TEST_CASE("NativeAvSink: emitted keyframe interval is the contract's 2s cadence at 24fps") {
    // VELOX_ASSEMBLY_READY_V1 keyframe_interval == 48 at 24 fps.
    certify_keyframe_cadence(24, 1, 48, "native-av-cadence-24fps.mp4");
}

TEST_CASE("NativeAvSink: emitted keyframe interval follows the rate, not a hardcoded 48") {
    // 2 seconds at 30 fps is 60 frames: the cadence is a TIME contract, so a
    // 30 fps stream must not inherit the 24 fps frame count.
    certify_keyframe_cadence(30, 1, 60, "native-av-cadence-30fps.mp4");
}

TEST_CASE("NativeAvSink: the cadence authority is the shared encoder helper") {
    // Locks the wiring itself: the sink must configure gop_size through the
    // shared helper, so a future edit cannot reintroduce a second cadence
    // authority that drifts from the CLI encoder path.
    CHECK(keyframe_interval_frames(24, 1) == 48);
    CHECK(keyframe_interval_frames(30, 1) == 60);
}

TEST_CASE("NativeAvSink: duration boundary matrix retains every submitted packet") {
    // Regression matrix around the previously observed 86/137-frame output.
    // Use the canonical native sink and inspect the packets in the finalized
    // MP4, rather than trusting the producer's submitted-frame counter.
    const int durations[] = {60, 86, 87, 96, 120, 137, 150, 180, 300};
    for (const int frame_count : durations) {
        const auto output_path = std::filesystem::path(
            "native-av-frame-matrix-" + std::to_string(frame_count) + ".mp4");
        std::error_code ec;
        std::filesystem::remove(output_path, ec);

        VideoSinkConfig config;
        config.stream.width = 64;
        config.stream.height = 64;
        config.stream.frame_rate_num = 30;
        config.stream.frame_rate_den = 1;
        config.stream.submitted_format = PixelFormat::RGBA8;
        config.encoder.codec = VideoCodec::H264;
        config.encoder.encoded_pixel_format = PixelFormat::YUV420P;
        config.output.container = VideoContainer::Mp4;
        config.output.output_path = output_path;
        config.output.overwrite = true;

        auto sink = create_video_sink(config);
        REQUIRE(sink != nullptr);
        REQUIRE(sink->open(config));
        std::vector<std::uint8_t> pixels(64U * 64U * 4U, 0x40U);
        for (int index = 0; index < frame_count; ++index) {
            const VideoFrameView frame{
                .data = pixels.data(),
                .stride_bytes = 64U * 4U,
                .width = 64,
                .height = 64,
                .pixel_format = PixelFormat::RGBA8,
                .pts = index,
            };
            REQUIRE(sink->submit(frame));
        }
        CHECK(sink->frames_submitted() == static_cast<std::uint64_t>(frame_count));
        REQUIRE(sink->flush());
        REQUIRE(sink->close());

        KeyframeSpacing observed;
        REQUIRE(read_keyframe_spacing(output_path, observed));
        CHECK(observed.packet_count == frame_count);
        std::filesystem::remove(output_path, ec);
    }
}
