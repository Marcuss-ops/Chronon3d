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
#include <libavutil/channel_layout.h>
}

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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
#if LIBAVCODEC_VERSION_MAJOR >= 59
    av_channel_layout_default(&audio_params->ch_layout, 1);
#else
    audio_params->channel_layout = AV_CH_LAYOUT_MONO;
#endif
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
        auto owned = chronon3d::media::OwnedAVPacket(av_packet_alloc());
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
        REQUIRE(mux.submit_video({std::move(video), AVRational{1, 10}, pts == 0}));
        auto audio_packet = packet(1, pts * 800, 800, 2);
        REQUIRE(mux.submit_audio({std::move(audio_packet), AVRational{1, 8000}, true}));
    }
    REQUIRE(mux.finalize());

    // The mux digest is OPT-IN (MuxOpenConfig::checksum) and this session did
    // not ask for it, so no digest was produced and nothing was read back.
    // That is the contract of the exporter/container lane: its published
    // artifact digest belongs to the output-contract verification
    // (verify_output_contract -> sha256), which is a mandatory pass over the
    // same file. Paying for a second, unread digest cost a full re-read of
    // every artifact on a seekable container — measured live as
    // full_file_reread_count=1 with final_reread_bytes equal to the whole file.
    const auto hash_metrics = mux.hash_metrics();
    CHECK(hash_metrics.output_write_bytes > 0);
    CHECK(hash_metrics.output_seek_count > 0);
    CHECK(hash_metrics.output_overwrite_bytes > 0);
    CHECK_FALSE(mux.checksum_requested());
    CHECK(mux.output_checksum().empty());
    CHECK_FALSE(mux.checksum_used_reread());
    CHECK(hash_metrics.incremental_hash_invalidations == 0);
    CHECK(hash_metrics.final_reread_bytes == 0);

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

// The opt-in side of the same contract, so the two cannot drift apart: a
// caller that ASKS for the digest gets the digest of the final bytes, and pays
// the documented seek/backpatch reread for them (MP4 patches the mdat size near
// the start of the file after the payload, which an append-only hash cannot
// survive). `assemble_segments` is that caller.
TEST_CASE("MuxSession checksum is opt-in and authoritative when requested") {
    const auto artifact = temp_path("checksum_optin.mp4");
    AVCodecContext* video_codec = avcodec_alloc_context3(nullptr);
    REQUIRE(video_codec != nullptr);
    video_codec->codec_type = AVMEDIA_TYPE_VIDEO;
    video_codec->codec_id = AV_CODEC_ID_MPEG4;
    video_codec->width = 32;
    video_codec->height = 32;
    video_codec->pix_fmt = AV_PIX_FMT_YUV420P;
    video_codec->time_base = AVRational{1, 10};

    chronon3d::media::MuxSession mux;
    std::string reason;
    REQUIRE(mux.open(chronon3d::media::MuxOpenConfig{
        .output_path = artifact,
        .video_codec = video_codec,
        .checksum = true}, reason));

    for (int64_t pts = 0; pts < 10; ++pts) {
        auto owned = chronon3d::media::OwnedAVPacket(av_packet_alloc());
        REQUIRE(owned != nullptr);
        REQUIRE(av_new_packet(owned.get(), 4) >= 0);
        owned->pts = pts;
        owned->dts = pts;
        owned->duration = 1;
        REQUIRE(mux.submit_video(chronon3d::media::EncodedPacket{
            std::move(owned), AVRational{1, 10}, pts == 0}));
    }
    REQUIRE(mux.finalize());

    CHECK(mux.checksum_requested());
    const auto hash_metrics = mux.hash_metrics();
    // The requested digest must be the digest of the bytes on disk: that is the
    // whole point of the fallback, and it is why the caller must justify it.
    const auto expected = chronon3d::assets::sha256_file(artifact);
    REQUIRE(expected.has_value());
    CHECK(mux.output_checksum() == expected->hex());
    CHECK(mux.checksum_used_reread());
    CHECK(hash_metrics.incremental_hash_invalidations == 1);
    CHECK(hash_metrics.final_reread_bytes ==
          static_cast<std::uint64_t>(std::filesystem::file_size(artifact)));

    avcodec_free_context(&video_codec);
    std::error_code ec;
    std::filesystem::remove(artifact, ec);
}

// The fragmented-mp4 side of the same contract: moof/mdat fragments have no
// start-of-file backpatch, so the append-only hash survives to the last byte —
// the checksum is produced WITHOUT the documented reread, and the bytes are
// final while they are appended (the overlay-artifact streaming upload lane).
TEST_CASE("MuxSession fragmented MP4 writes fragments and skips the checksum reread") {
    const auto artifact = temp_path("fragmented.mp4");
    AVCodecContext* video_codec = avcodec_alloc_context3(nullptr);
    REQUIRE(video_codec != nullptr);
    video_codec->codec_type = AVMEDIA_TYPE_VIDEO;
    video_codec->codec_id = AV_CODEC_ID_MPEG4;
    video_codec->width = 32;
    video_codec->height = 32;
    video_codec->pix_fmt = AV_PIX_FMT_YUV420P;
    video_codec->time_base = AVRational{1, 10};

    chronon3d::media::MuxSession mux;
    std::string reason;
    REQUIRE(mux.open(chronon3d::media::MuxOpenConfig{
        .output_path = artifact,
        .video_codec = video_codec,
        .checksum = true,
        .fragmented_mp4 = true}, reason));

    for (int64_t pts = 0; pts < 10; ++pts) {
        auto owned = chronon3d::media::OwnedAVPacket(av_packet_alloc());
        REQUIRE(owned != nullptr);
        REQUIRE(av_new_packet(owned.get(), 4) >= 0);
        owned->pts = pts;
        owned->dts = pts;
        owned->duration = 1;
        REQUIRE(mux.submit_video(chronon3d::media::EncodedPacket{
            std::move(owned), AVRational{1, 10}, pts == 0}));
    }
    REQUIRE(mux.finalize());

    // The digest is authoritative AND append-only: fragmented output has no
    // mdat-size backpatch, so nothing is invalidated and nothing is re-read.
    const auto expected = chronon3d::assets::sha256_file(artifact);
    REQUIRE(expected.has_value());
    CHECK(mux.output_checksum() == expected->hex());
    CHECK_FALSE(mux.checksum_used_reread());
    const auto hash_metrics = mux.hash_metrics();
    CHECK(hash_metrics.incremental_hash_invalidations == 0);
    CHECK(hash_metrics.final_reread_bytes == 0);

    // The container is really fragmented: ftyp + moof boxes are present.
    std::ifstream in(artifact, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(in),
                            std::istreambuf_iterator<char>()};
    CHECK(bytes.find("ftyp") != std::string::npos);
    CHECK(bytes.find("moof") != std::string::npos);

    avcodec_free_context(&video_codec);
    std::error_code ec;
    std::filesystem::remove(artifact, ec);
}

// A/B parity gate (TICKET-FMP4-STREAMING): the same encoded packets muxed
// into a classic MP4 and into a fragmented MP4 must decode to the SAME frames.
// The container may differ (moov placement, moof fragments); the decoded
// pixels may not. Frame identity is pinned with the suite's canonical FNV-1a
// display framehash over the raw planes — the same convention as the Vulkan
// parity tests — so a wrong timestamp, dropped packet or reordered frame
// changes the hash sequence.
TEST_CASE("MuxSession A/B: classic and fragmented outputs decode to identical frames") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg CLI required");
        return;
    }

    const auto source = temp_path("ab_source.mp4");
    const auto classic = temp_path("ab_classic.mp4");
    const auto fragmented = temp_path("ab_fragmented.mp4");
    const auto make =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i testsrc2=s=64x48:r=10 -frames:v 24 "
        "-c:v libx264 -pix_fmt yuv420p -g 8 " + source;
    REQUIRE(std::system(make.c_str()) == 0);

    // Read the source packets ONCE and submit the same sequence to both muxes.
    AVFormatContext* input = nullptr;
    REQUIRE(avformat_open_input(&input, source.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(input, nullptr) >= 0);
    const int vidx = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(vidx >= 0);
    AVStream* in_stream = input->streams[vidx];

    struct SavedPacket {
        chronon3d::media::OwnedAVPacket packet;
        bool keyframe{false};
    };
    std::vector<SavedPacket> packets;
    AVPacket* read_packet = av_packet_alloc();
    REQUIRE(read_packet != nullptr);
    while (av_read_frame(input, read_packet) >= 0) {
        if (read_packet->stream_index == vidx) {
            auto owned = chronon3d::media::OwnedAVPacket(av_packet_alloc());
            REQUIRE(owned != nullptr);
            REQUIRE(av_packet_ref(owned.get(), read_packet) >= 0);
            packets.push_back({std::move(owned),
                               (read_packet->flags & AV_PKT_FLAG_KEY) != 0});
        }
        av_packet_unref(read_packet);
    }
    av_packet_free(&read_packet);
    REQUIRE(packets.size() == 24);

    const auto mux_into = [&](const std::string& out, bool fragmented) {
        chronon3d::media::MuxSession mux;
        std::string reason;
        REQUIRE(mux.open(chronon3d::media::MuxOpenConfig{
            .output_path = out,
            .video_params = in_stream->codecpar,
            .video_time_base = in_stream->time_base,
            .checksum = true,
            .fragmented_mp4 = fragmented}, reason));
        for (const auto& saved : packets) {
            auto copy = chronon3d::media::OwnedAVPacket(av_packet_alloc());
            REQUIRE(copy != nullptr);
            REQUIRE(av_packet_ref(copy.get(), saved.packet.get()) >= 0);
            REQUIRE(mux.submit_video(chronon3d::media::EncodedPacket{
                std::move(copy), in_stream->time_base, saved.keyframe}));
        }
        REQUIRE(mux.finalize());
        // Both containers must produce the digest of their own final bytes.
        const auto expected = chronon3d::assets::sha256_file(out);
        REQUIRE(expected.has_value());
        CHECK(mux.output_checksum() == expected->hex());
    };
    mux_into(classic, false);
    mux_into(fragmented, true);
    avformat_close_input(&input);

    // Decode every frame of each output and hash the raw planes (canonical
    // FNV-1a display framehash, same as the Vulkan parity tests).
    const auto decode_hashes = [](const std::string& path) {
        std::vector<std::uint64_t> hashes;
        AVFormatContext* demux = nullptr;
        if (avformat_open_input(&demux, path.c_str(), nullptr, nullptr) < 0) {
            return hashes;
        }
        if (avformat_find_stream_info(demux, nullptr) < 0) {
            avformat_close_input(&demux);
            return hashes;
        }
        const int idx = av_find_best_stream(demux, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (idx < 0) {
            avformat_close_input(&demux);
            return hashes;
        }
        AVCodecParameters* params = demux->streams[idx]->codecpar;
        AVCodecContext* decoder =
            avcodec_alloc_context3(avcodec_find_decoder(params->codec_id));
        if (!decoder || avcodec_parameters_to_context(decoder, params) < 0 ||
            avcodec_open2(decoder, avcodec_find_decoder(params->codec_id),
                          nullptr) < 0) {
            avcodec_free_context(&decoder);
            avformat_close_input(&demux);
            return hashes;
        }
        AVFrame* frame = av_frame_alloc();
        AVPacket* pkt = av_packet_alloc();
        const auto hash_frame = [&](const AVFrame* f) {
            std::uint64_t h = 14695981039346656037ULL;
            for (int plane = 0; plane < 3; ++plane) {
                const int height = plane == 0 ? f->height : f->height / 2;
                const int width = plane == 0 ? f->width : f->width / 2;
                for (int y = 0; y < height; ++y) {
                    const auto* row = f->data[plane] + y * f->linesize[plane];
                    for (int x = 0; x < width; ++x) {
                        h ^= row[x];
                        h *= 1099511628211ULL;
                    }
                }
            }
            return h;
        };
        while (av_read_frame(demux, pkt) >= 0) {
            if (pkt->stream_index == idx && avcodec_send_packet(decoder, pkt) >= 0) {
                while (avcodec_receive_frame(decoder, frame) >= 0) {
                    hashes.push_back(hash_frame(frame));
                    av_frame_unref(frame);
                }
            }
            av_packet_unref(pkt);
        }
        avcodec_send_packet(decoder, nullptr);
        while (avcodec_receive_frame(decoder, frame) >= 0) {
            hashes.push_back(hash_frame(frame));
            av_frame_unref(frame);
        }
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&decoder);
        avformat_close_input(&demux);
        return hashes;
    };

    const auto classic_hashes = decode_hashes(classic);
    const auto fragmented_hashes = decode_hashes(fragmented);
    REQUIRE(classic_hashes.size() == 24);
    REQUIRE(fragmented_hashes.size() == 24);
    for (size_t i = 0; i < classic_hashes.size(); ++i) {
        MESSAGE("frame " << i << " classic=" << classic_hashes[i]
                         << " fragmented=" << fragmented_hashes[i]);
        CHECK(classic_hashes[i] == fragmented_hashes[i]);
    }

    std::error_code ec;
    std::filesystem::remove(source, ec);
    std::filesystem::remove(classic, ec);
    std::filesystem::remove(fragmented, ec);
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
    CHECK_FALSE(result.ffprobe_ms.has_value());  // nothing ran
    CHECK_FALSE(result.sha256_ms.has_value());   // nothing computed
    CHECK_FALSE(result.probe_facts.has_value());  // nothing observed
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
    REQUIRE(result.ffprobe_ms.has_value());
    CHECK(*result.ffprobe_ms > 0.0);  // legacy field: in-process probe ran
    REQUIRE(result.sha256_ms.has_value());
    CHECK(*result.sha256_ms > 0.0);   // digest actually computed

    // Fix 2 (TICKET-OVERLAY-THROUGHPUT, dedup): the probe observation is
    // preserved on the result so a downstream receipt builder reuses it
    // instead of probing the same bytes a second time.
    REQUIRE(result.probe_facts.has_value());
    CHECK(result.probe_facts->format_name.find("mp4") != std::string::npos);
    REQUIRE(result.probe_facts->streams.size() == 1);
    CHECK(result.probe_facts->streams[0].width == 1920);
    CHECK(result.probe_facts->streams[0].height == 1080);
    CHECK(result.probe_facts->streams[0].frame_count == 3);

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

TEST_CASE("verify_output_contract: frame loss fails render verification") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg CLI required");
        return;
    }
    const auto artifact = temp_path("truncated_frame_count.mp4");
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
    contract.frame_count = 4;

    const auto result = verify_output_contract(artifact, contract);
    CHECK_FALSE(result.passed);
    CHECK_FALSE(result.copy_eligible);
    CHECK(result.failure.find("frame count mismatch") != std::string::npos);
    CHECK_FALSE(result.sha256.empty());

    std::error_code ec;
    std::filesystem::remove(artifact, ec);
}
