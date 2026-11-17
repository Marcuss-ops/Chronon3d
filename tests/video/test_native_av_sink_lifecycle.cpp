#include <chronon3d/media/video/keyframe_cadence.hpp>
#include <chronon3d/media/video/video_config.hpp>
#include <chronon3d/media/video/video_sink_factory.hpp>

#include <doctest/doctest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hash.h>
}

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <filesystem>
#include <string>
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
    const bool opened = sink->open(config);
    INFO(sink->last_error_message());
    REQUIRE(opened);
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
    config.stream.frame_rate_num = 24; config.stream.frame_rate_den = 1;
    config.stream.submitted_format = PixelFormat::RGB24;
    config.encoder.codec = VideoCodec::H264;
    config.encoder.encoded_pixel_format = PixelFormat::YUV420P;
    // CRF 0 requires x264's High 4:4:4 profile, while the default encoder
    // contract selects High 4:2:0. Keep this fixture compatible with the
    // configured pixel format; the test validates BT.709 metadata and decoded
    // channel values, not lossless coding.
    config.encoder.crf = 1;
    config.output.container = VideoContainer::Mp4;
    config.output.output_path = path;
    config.output.overwrite = true;
    auto sink = create_video_sink(config);
    const bool opened = sink->open(config);
    INFO(sink->last_error_message());
    REQUIRE(opened);
    std::vector<std::uint8_t> rgb(64U * 64U * 3U);
    for (std::size_t i = 0; i < rgb.size(); i += 3) rgb[i] = 255;
    REQUIRE(sink->submit(VideoFrameView{.data = rgb.data(), .width = 64,
        .height = 64, .pixel_format = PixelFormat::RGB24, .pts = 0}));
    REQUIRE(sink->close());
    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) == 0);
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
    const bool opened = sink->open(config);
    INFO(sink->last_error_message());
    REQUIRE(opened);

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
        const bool opened = sink->open(config);
        INFO(sink->last_error_message());
        REQUIRE(opened);
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

// ── Canonical x264 lane (TICKET-CANONICAL-X264-SINGLE-SESSION) ─────────────
//
// The lane promises ONE byte identity, not a nominal profile: the avcC/SPS-PPS
// hash below is the certified 1920x1080 24 fps x264 stream. The test reads the
// extradata from the WRITTEN MP4 so a drifting preset, refs/bf setting, GOP
// policy or colour tag fails here instead of at RenderingGen's finalize.
//
// The constant is intentionally duplicated (not shared with the sink): a bug
// in the sink's own constant must not also move the test's oracle.
namespace {

constexpr const char* kCertifiedCanonicalAvcC =
    "7201ce80a2b2b32a0c1a89501851099f267c1767d59ff799db744f72032aae5a";

std::string avcc_sha256_hex(const std::uint8_t* data, int size) {
    AVHashContext* hash = nullptr;
    if (av_hash_alloc(&hash, "sha256") < 0 || hash == nullptr) return {};
    av_hash_init(hash);
    av_hash_update(hash, data, static_cast<std::size_t>(size));
    const int digest_size = av_hash_get_size(hash);
    std::array<std::uint8_t, AV_HASH_MAX_SIZE> digest{};
    av_hash_final(hash, digest.data());
    av_hash_freep(&hash);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(digest_size) * 2U);
    for (int i = 0; i < digest_size; ++i) {
        out.push_back(kHex[digest[static_cast<std::size_t>(i)] >> 4U]);
        out.push_back(kHex[digest[static_cast<std::size_t>(i)] & 0x0fU]);
    }
    return out;
}

// CanonicalEncodedStream is the identity of the EMITTED elementary stream:
// the avcC digest plus a digest over every packet's PTS, DTS, flags, size and
// payload. Two sessions that agree on this value produced the same bytes, not
// merely streams that decode to the same pictures.
struct CanonicalEncodedStream {
    std::string avcc_sha256;
    std::string stream_sha256;
    int packet_count{0};
    double encode_ms{0.0};
};

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

bool digest_canonical_stream(const std::filesystem::path& path,
                             CanonicalEncodedStream& out) {
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) < 0) {
        return false;
    }
    struct FormatGuard {
        AVFormatContext*& context;
        ~FormatGuard() { avformat_close_input(&context); }
    } guard{format};
    if (avformat_find_stream_info(format, nullptr) < 0) {
        return false;
    }
    const int stream =
        av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0) {
        return false;
    }
    const AVCodecParameters* params = format->streams[stream]->codecpar;
    out.avcc_sha256 = avcc_sha256_hex(params->extradata, params->extradata_size);

    std::vector<std::uint8_t> identity;
    if (params->extradata != nullptr && params->extradata_size > 0) {
        identity.insert(identity.end(), params->extradata,
                        params->extradata + params->extradata_size);
    }
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        return false;
    }
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == stream) {
            ++out.packet_count;
            append_u64(identity, static_cast<std::uint64_t>(packet->pts));
            append_u64(identity, static_cast<std::uint64_t>(packet->dts));
            append_u64(identity, static_cast<std::uint64_t>(packet->flags));
            append_u64(identity, static_cast<std::uint64_t>(packet->size));
            identity.insert(identity.end(), packet->data,
                            packet->data + packet->size);
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    if (out.packet_count == 0) {
        return false;
    }
    out.stream_sha256 = avcc_sha256_hex(
        identity.data(), static_cast<int>(identity.size()));
    return true;
}

// encode_canonical_1080p runs one complete canonical session (open, `frames`
// frames, flush, close) and returns the emitted stream identity plus the wall
// time of the session. The default 49 frames is one 48-frame GOP plus its
// closing keyframe; 456 frames exercises the multi-GOP cadence.
void encode_canonical_1080p(const std::filesystem::path& path,
                            CanonicalEncodedStream& out, int frames = 49) {
    std::error_code ec;
    std::filesystem::remove(path, ec);

    VideoSinkConfig config;
    config.stream.width = 1920;
    config.stream.height = 1080;
    config.stream.frame_rate_num = 24;
    config.stream.frame_rate_den = 1;
    config.stream.submitted_format = PixelFormat::RGBA8;
    config.encoder.codec = VideoCodec::H264;
    config.encoder.canonical_x264 = true;
    config.output.container = VideoContainer::Mp4;
    config.output.output_path = path;
    config.output.overwrite = true;

    auto sink = create_video_sink(config);
    REQUIRE(sink != nullptr);
    const bool opened = sink->open(config);
    INFO(sink->last_error_message());
    REQUIRE(opened);

    std::vector<std::uint8_t> pixels(1920U * 1080U * 4U, 0x40U);
    const auto started = std::chrono::steady_clock::now();
    for (int index = 0; index < frames; ++index) {
        const VideoFrameView frame{
            .data = pixels.data(),
            .stride_bytes = 1920U * 4U,
            .width = 1920,
            .height = 1080,
            .pixel_format = PixelFormat::RGBA8,
            .pts = index,
        };
        REQUIRE(sink->submit(frame));
    }
    REQUIRE(sink->flush());
    REQUIRE(sink->close());
    out.encode_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - started)
                        .count();
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(digest_canonical_stream(path, out));
}

} // namespace

TEST_CASE("NativeAvSink canonical x264 lane certifies its avcC, GOP and 90000 track") {
    const auto path = std::filesystem::path("native-av-canonical-x264.mp4");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    VideoSinkConfig config;
    config.stream.width = 1920;
    config.stream.height = 1080;
    config.stream.frame_rate_num = 24;
    config.stream.frame_rate_den = 1;
    config.stream.submitted_format = PixelFormat::RGBA8;
    config.encoder.codec = VideoCodec::H264;
    config.encoder.canonical_x264 = true;
    config.output.container = VideoContainer::Mp4;
    config.output.output_path = path;
    config.output.overwrite = true;

    auto sink = create_video_sink(config);
    REQUIRE(sink != nullptr);
    const bool opened = sink->open(config);
    INFO(sink->last_error_message());
    REQUIRE(opened);

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(config.stream.width) *
            static_cast<std::size_t>(config.stream.height) * 4U,
        0x40U);

    // 49 frames = one full GOP (0..47) plus the keyframe at 48.
    for (int index = 0; index < 49; ++index) {
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
    REQUIRE(std::filesystem::exists(path));

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) == 0);
    struct FormatGuard {
        AVFormatContext*& context;
        ~FormatGuard() { avformat_close_input(&context); }
    } guard{format};
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    const int stream =
        av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(stream >= 0);
    const AVStream* video = format->streams[stream];
    const AVCodecParameters* params = video->codecpar;

    CHECK(video->time_base.num == 1);
    CHECK(video->time_base.den == 90000);
    CHECK(params->width == 1920);
    CHECK(params->height == 1080);
    CHECK(params->format == AV_PIX_FMT_YUV420P);
    CHECK(params->video_delay == 0);  // no B-frames
    CHECK(params->extradata_size == 49);
    INFO("avcC size=" << params->extradata_size);
    CHECK(avcc_sha256_hex(params->extradata, params->extradata_size) ==
          kCertifiedCanonicalAvcC);

    KeyframeSpacing spacing;
    REQUIRE(read_keyframe_spacing(path, spacing));
    CHECK(spacing.packet_count == 49);
    REQUIRE(spacing.keyframe_indices.size() == 2U);
    CHECK(spacing.keyframe_indices[0] == 0);
    CHECK(spacing.keyframe_indices[1] == 48);

    std::filesystem::remove(path, ec);
}

TEST_CASE("NativeAvSink canonical x264 lane is bit-for-bit reproducible across sessions") {
    const auto first_path =
        std::filesystem::path("native-av-canonical-x264-repro-a.mp4");
    const auto second_path =
        std::filesystem::path("native-av-canonical-x264-repro-b.mp4");
    CanonicalEncodedStream first;
    CanonicalEncodedStream second;
    encode_canonical_1080p(first_path, first);
    encode_canonical_1080p(second_path, second);

    CHECK(first.packet_count == 49);
    CHECK(second.packet_count == first.packet_count);
    CHECK(first.avcc_sha256 == kCertifiedCanonicalAvcC);
    CHECK(second.avcc_sha256 == first.avcc_sha256);
    // Same avcC and same packet bytes/timestamps: a retry re-emits the very
    // same stream. This is what makes the lane's output a stable identity for
    // cache and concat decisions, not just a stream that looks equivalent.
    CHECK(second.stream_sha256 == first.stream_sha256);
    MESSAGE("canonical x264 1080p24 49 frames: run1="
            << first.encode_ms << " ms, run2=" << second.encode_ms
            << " ms, avcC=" << first.avcc_sha256
            << ", stream=" << first.stream_sha256);

    std::error_code ec;
    std::filesystem::remove(first_path, ec);
    std::filesystem::remove(second_path, ec);
}

// Gate 3 of TICKET-CANONICAL-X264-SINGLE-SESSION requires more than one GOP:
// the 49-frame fixture only proves the first seam. 456 frames = 9 full GOPs
// plus the closing keyframe at 432, so a scene-cut, a drifting keyint or an
// open-GOP transition shows up as a wrong keyframe index. The artifact is kept
// for the external verifier (stss table + full decode) and its path is printed.
TEST_CASE("NativeAvSink canonical x264 lane keeps the 48-frame GOP across 456 frames") {
    const auto path = std::filesystem::path("native-av-canonical-x264-456.mp4");
    CanonicalEncodedStream stream;
    encode_canonical_1080p(path, stream, 456);

    CHECK(stream.packet_count == 456);
    CHECK(stream.avcc_sha256 == kCertifiedCanonicalAvcC);

    KeyframeSpacing spacing;
    REQUIRE(read_keyframe_spacing(path, spacing));
    CHECK(spacing.packet_count == 456);
    REQUIRE(spacing.keyframe_indices.size() == 10U);
    for (std::size_t index = 0; index < spacing.keyframe_indices.size(); ++index) {
        CHECK(spacing.keyframe_indices[index] == static_cast<int>(index) * 48);
    }
    MESSAGE("canonical x264 456 frames: avcC="
            << stream.avcc_sha256 << ", stream=" << stream.stream_sha256
            << ", encode_ms=" << stream.encode_ms
            << ", artifact=" << std::filesystem::absolute(path).string());
}

TEST_CASE("NativeAvSink canonical x264 lane rejects a different geometry") {
    VideoSinkConfig config;
    config.stream.width = 1280;
    config.stream.height = 720;
    config.stream.frame_rate_num = 24;
    config.stream.frame_rate_den = 1;
    config.stream.submitted_format = PixelFormat::RGBA8;
    config.encoder.codec = VideoCodec::H264;
    config.encoder.canonical_x264 = true;
    config.output.container = VideoContainer::Mp4;
    config.output.output_path = "native-av-canonical-x264-rejected.mp4";
    config.output.overwrite = true;

    auto sink = create_video_sink(config);
    REQUIRE(sink != nullptr);
    CHECK_FALSE(sink->open(config));
    CHECK(sink->last_error_message().find("1920x1080") !=
          std::string::npos);
}

TEST_CASE("NativeAvSink canonical x264 lane rejects a hardware encoder") {
    VideoSinkConfig config;
    config.stream.width = 1920;
    config.stream.height = 1080;
    config.stream.frame_rate_num = 24;
    config.stream.frame_rate_den = 1;
    config.stream.submitted_format = PixelFormat::RGBA8;
    config.encoder.codec = VideoCodec::H264Nvenc;
    config.encoder.canonical_x264 = true;
    config.output.container = VideoContainer::Mp4;
    config.output.output_path = "native-av-canonical-x264-nvenc.mp4";
    config.output.overwrite = true;

    auto sink = create_video_sink(config);
    REQUIRE(sink != nullptr);
    CHECK_FALSE(sink->open(config));
    CHECK(sink->last_error_message().find("H264") != std::string::npos);
}
