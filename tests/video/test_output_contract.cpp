// ═══════════════════════════════════════════════════════════════════════════
// tests/video/test_output_contract.cpp
//
// Locks the canonical OutputContract resolution + verification contract:
//   - youtube_overlay_v1 resolves to 1920×1080 / 24fps / h264 / yuv420p
//     with audio policy, and unknown profiles fail loud.
//   - assets::sha256_file hashes a file's bytes with the canonical SHA-256
//     primitive (identical to sha256_string of the same bytes).
//   - verify_output_contract fails structural checks on missing files, and
//     separates `passed` (decodable + geometry) from `copy_eligible`
//     (media contract + SHA-256).
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/video/output_contract.hpp>
#include <chronon3d/media/video/packet_assembler.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using chronon3d::media::video::OutputContract;
using chronon3d::media::video::resolve_output_contract;
using chronon3d::media::video::verify_output_contract;

std::string temp_path(const char* suffix) {
    static std::atomic<unsigned> counter{0};
    return "/tmp/chronon3d_output_contract_" +
           std::to_string(counter.fetch_add(1)) + "_" + suffix;
}

bool ffmpeg_available() {
    return std::system("ffmpeg -version >/dev/null 2>&1") == 0;
}

}  // namespace

TEST_CASE("resolve_output_contract: youtube_overlay_v1 resolves canonically") {
    auto result = resolve_output_contract("youtube_overlay_v1");
    REQUIRE(result);
    const auto& c = result.value();
    CHECK(c.width == 1920);
    CHECK(c.height == 1080);
    CHECK(c.fps.num() == 24);
    CHECK(c.fps.den() == 1);
    CHECK(c.video_codec == "h264");
    CHECK(c.pixel_format == "yuv420p");
    CHECK(c.audio_required);
    CHECK(c.audio_streams == 1);
}

TEST_CASE("MuxSession A/V smoke test keeps streams and timestamps coherent") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg CLI required");
        return;
    }

    const auto artifact = temp_path("av_smoke.mp4");
    AVCodecContext* video_codec = avcodec_alloc_context3(nullptr);
    REQUIRE(video_codec != nullptr);
    video_codec->codec_type = AVMEDIA_TYPE_VIDEO;
    video_codec->codec_id = AV_CODEC_ID_MPEG4;
    video_codec->width = 32;
    video_codec->height = 32;
    video_codec->pix_fmt = AV_PIX_FMT_YUV420P;
    video_codec->time_base = AVRational{1, 10};

    AVCodecParameters* audio_params = avcodec_parameters_alloc();
    REQUIRE(audio_params != nullptr);
    audio_params->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_params->codec_id = AV_CODEC_ID_AAC;
    audio_params->sample_rate = 8000;
    audio_params->channel_layout = AV_CH_LAYOUT_MONO;
    audio_params->format = AV_SAMPLE_FMT_FLTP;
    audio_params->bit_rate = 64000;

    chronon3d::media::MuxSession mux;
    chronon3d::media::AudioStreamConfig audio{
        .params = audio_params, .time_base = AVRational{1, 8000}};
    std::string reason;
    REQUIRE(mux.open(chronon3d::media::MuxOpenConfig{
        .output_path = artifact,
        .video_codec = video_codec,
        .audio = audio}, reason));
    CHECK(mux.has_audio());

    auto packet = [](int stream, int64_t pts, int duration, int size) {
        auto owned = std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* p) {
            av_packet_free(&p);
        });
        REQUIRE(owned != nullptr);
        REQUIRE(av_new_packet(owned.get(), size) >= 0);
        owned->pts = pts;
        owned->dts = pts;
        owned->duration = duration;
        owned->stream_index = stream;
        return owned;
    };

    for (int64_t pts = 0; pts < 10; ++pts) {
        auto video = packet(0, pts, 1, 4);
        REQUIRE(mux.submit_video({video, AVRational{1, 10}, pts == 0}));
        auto audio_packet = packet(1, pts * 800, 800, 2);
        REQUIRE(mux.submit_audio({audio_packet, AVRational{1, 8000}, true}));
    }
    REQUIRE(mux.finalize());

    AVFormatContext* input = nullptr;
    REQUIRE(avformat_open_input(&input, artifact.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(input, nullptr) >= 0);
    REQUIRE(input->nb_streams == 2);
    int video_index = -1;
    int audio_index = -1;
    for (unsigned i = 0; i < input->nb_streams; ++i) {
        if (input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) video_index = static_cast<int>(i);
        if (input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) audio_index = static_cast<int>(i);
    }
    REQUIRE(video_index >= 0);
    REQUIRE(audio_index >= 0);
    CHECK(input->streams[video_index]->duration > 0);
    CHECK(input->streams[audio_index]->duration > 0);

    int64_t last_video = AV_NOPTS_VALUE;
    int64_t last_audio = AV_NOPTS_VALUE;
    AVPacket* read_packet = av_packet_alloc();
    REQUIRE(read_packet != nullptr);
    while (av_read_frame(input, read_packet) >= 0) {
        if (read_packet->stream_index == video_index) {
            const bool video_monotonic = last_video == AV_NOPTS_VALUE ||
                read_packet->dts >= last_video;
            CHECK(video_monotonic);
            last_video = read_packet->dts;
        } else if (read_packet->stream_index == audio_index) {
            const bool audio_monotonic = last_audio == AV_NOPTS_VALUE ||
                read_packet->dts >= last_audio;
            CHECK(audio_monotonic);
            last_audio = read_packet->dts;
        }
        av_packet_unref(read_packet);
    }
    av_packet_free(&read_packet);
    avformat_close_input(&input);
    avcodec_free_context(&video_codec);
    avcodec_parameters_free(&audio_params);

    CHECK(last_video >= 0);
    CHECK(last_audio >= 0);
    std::error_code ec;
    std::filesystem::remove(artifact, ec);
}

TEST_CASE("resolve_output_contract: unknown profile fails loud") {
    auto result = resolve_output_contract("does_not_exist");
    REQUIRE_FALSE(result);
    CHECK(result.error().find("does_not_exist") != std::string::npos);
}

TEST_CASE("assets::sha256_file matches sha256_string of the same bytes") {
    const std::string payload = "chronon-output-contract-sha256-fixture";
    const auto path = temp_path("hash.bin");
    {
        std::ofstream out(path, std::ios::binary);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    const auto file_digest = chronon3d::assets::sha256_file(path);
    REQUIRE(file_digest.has_value());
    const auto string_digest = chronon3d::assets::sha256_string(payload);
    CHECK(file_digest->hex() == string_digest.hex());
    CHECK(file_digest->hex().size() == 64);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("assets::sha256_file returns nullopt for a missing file") {
    CHECK_FALSE(chronon3d::assets::sha256_file(
        "/tmp/chronon3d_output_contract_definitely_missing.bin").has_value());
}

TEST_CASE("verify_output_contract fails structural checks on a missing file") {
    OutputContract contract;
    contract.width = 1920;
    contract.height = 1080;
    contract.audio_required = false;
    contract.audio_streams = 0;

    const auto result = verify_output_contract(
        "/tmp/chronon3d_output_contract_missing.mp4", contract);
    REQUIRE_FALSE(result.passed);
    CHECK_FALSE(result.copy_eligible);
    CHECK(result.sha256.empty());  // no digest computed
    CHECK(result.ffprobe_ms == 0.0);  // nothing ran
    CHECK(result.sha256_ms == 0.0);   // nothing computed
}

TEST_CASE("verify_output_contract: full pass sets copy_eligible") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg CLI required");
        return;
    }
    const auto artifact = temp_path("verified.mp4");
    const std::string make =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i color=c=black:s=1920x1080:r=24 "
        "-frames:v 3 -c:v libx264 -pix_fmt yuv420p " + artifact;
    REQUIRE(std::system(make.c_str()) == 0);

    OutputContract contract;
    contract.width = 1920;
    contract.height = 1080;
    contract.fps = chronon3d::FrameRate{24, 1};
    contract.video_codec = "h264";
    contract.pixel_format = "yuv420p";
    contract.audio_required = false;
    contract.audio_streams = 0;
    contract.frame_count = 3;

    const auto result = verify_output_contract(artifact, contract);
    CHECK(result.passed);
    CHECK(result.copy_eligible);
    CHECK(result.width == 1920);
    CHECK(result.height == 1080);
    CHECK(result.video_codec == "h264");
    CHECK(result.pixel_format == "yuv420p");
    CHECK(result.sha256.size() == 64);
    CHECK(result.ffprobe_ms > 0.0);   // legacy field: in-process probe ran
    CHECK(result.sha256_ms > 0.0);    // digest actually computed

    std::error_code ec;
    std::filesystem::remove(artifact, ec);
}

TEST_CASE("verify_output_contract: pix_fmt mismatch passes but not copy_eligible") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg CLI required");
        return;
    }
    const auto artifact = temp_path("wrong_pixfmt.mp4");
    const std::string make =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i color=c=black:s=1920x1080:r=24 "
        "-frames:v 1 -c:v libx264 -pix_fmt yuv420p " + artifact;
    REQUIRE(std::system(make.c_str()) == 0);

    // Expect yuv444p — the artifact is decodable (passed) but violates the
    // media contract, so copy_eligible must stay false.
    OutputContract contract;
    contract.width = 1920;
    contract.height = 1080;
    contract.fps = chronon3d::FrameRate{24, 1};
    contract.video_codec = "h264";
    contract.pixel_format = "yuv444p";
    contract.audio_required = false;
    contract.audio_streams = 0;

    const auto result = verify_output_contract(artifact, contract);
    CHECK(result.passed);
    CHECK_FALSE(result.copy_eligible);
    CHECK_FALSE(result.sha256.empty());

    std::error_code ec;
    std::filesystem::remove(artifact, ec);
}
