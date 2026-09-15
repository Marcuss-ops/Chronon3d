// =============================================================================
// test_gop_direct_mux_parity.cpp — the BoundaryReencode parity gate (P0-5B).
//
// The legacy hybrid executor (`execute_hybrid_gop_plan`: one temp MP4 per
// segment, then remux) is scheduled for demolition (P0-5G). Demolition is only
// legal once the direct-mux path (COPY + REENCODE → one caller-owned
// MuxSession) produces a semantically identical output on a frozen corpus.
//
// This file owns that gate:
//
//   * `gop_parity_corpus()` freezes the authoring shapes the gate must cover,
//   * `probe_container()` extracts the oracle facts from a container,
//   * `compare_containers()` decides parity with the criteria the splice
//     boundary actually depends on:
//       - codec facts (codec / profile / level / dimensions / pixel format /
//         codec configuration record),
//       - every packet: payload byte identity (SHA-256), PTS, DTS, duration
//         and flags — NO tolerance for packets that must be copied verbatim,
//       - decoded frame count and container duration,
//       - audio stream facts.
//
// Whole-file SHA-256 is reported separately and is deliberately NOT the only
// criterion: a container can be semantically identical with a different
// layout/metadata. For COPIES, packet byte identity is absolute.
// =============================================================================

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::cli::test_gop_parity {

/// One authoring shape the direct-mux executor must reproduce. These are the
/// schedule shapes the legacy segment-file executor is the oracle for.
struct GopParityCase {
    std::string_view name;
    /// Comma-separated planned GOP kinds, in source order.
    std::string_view schedule;
    /// Number of disjoint modified blocks (content edits) in the timeline.
    std::size_t edited_blocks{0};
    /// The requested window is narrower than the source.
    bool trim{false};
};

/// The frozen corpus. Growing it is fine; removing a shape silently shrinks
/// the demolition gate, which is why the test pins every name.
[[nodiscard]] inline const std::vector<GopParityCase>& gop_parity_corpus() {
    static const std::vector<GopParityCase> corpus{
        {"copy_reencode_copy", "COPY,REENCODE,COPY", 1, false},
        {"reencode_copy", "REENCODE,COPY", 1, false},
        {"copy_reencode", "COPY,REENCODE", 1, false},
        {"two_separate_modified_blocks",
         "COPY,REENCODE,COPY,REENCODE,COPY", 2, false},
        {"trim_plus_content_edit", "COPY,REENCODE", 1, true},
    };
    return corpus;
}

} // namespace chronon3d::cli::test_gop_parity

TEST_CASE("BoundaryReencode parity corpus is frozen") {
    const auto& corpus = chronon3d::cli::test_gop_parity::gop_parity_corpus();
    CHECK(corpus.size() >= 5);
    const auto has = [&](std::string_view name) {
        for (const auto& entry : corpus) {
            if (entry.name == name) return true;
        }
        return false;
    };
    CHECK(has("copy_reencode_copy"));
    CHECK(has("reencode_copy"));
    CHECK(has("copy_reencode"));
    CHECK(has("two_separate_modified_blocks"));
    CHECK(has("trim_plus_content_edit"));
    // Every shape must describe at least one re-encode: a whole-clip copy is
    // covered by the BitstreamCopy lane, not by the boundary executor.
    for (const auto& entry : corpus) {
        INFO("case: " << entry.name);
        CHECK(entry.schedule.find("REENCODE") != std::string_view::npos);
    }
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hash.h>
}

#include "utils/video/gop_smart_copy.hpp"
// Full definition of MuxSession: the direct-mux test owns the session itself.
#include "chronon3d/media/video/packet_assembler.hpp"

namespace chronon3d::cli::test_gop_parity {

struct PacketFacts {
    std::int64_t pts{0};
    std::int64_t dts{0};
    std::int64_t duration{0};
    int flags{0};
    std::string payload_sha256;
};

struct StreamFacts {
    int codec_id{-1};
    int profile{-1};
    int level{-1};
    int pixel_format{-1};
    int width{0};
    int height{0};
    std::string extradata_sha256;
    std::vector<PacketFacts> packets;
    bool dts_monotonic{true};
};

struct AudioFacts {
    bool present{false};
    int codec_id{-1};
    int sample_rate{0};
    int channels{0};
    std::uint64_t packet_count{0};
};

struct ContainerFacts {
    bool valid{false};
    StreamFacts video{};
    AudioFacts audio{};
    std::int64_t duration_us{0};
    std::uint64_t decoded_video_frames{0};
    bool decoded_ok{false};
    /// Strongest signal, reported separately: it must never be the ONLY
    /// criterion (container layout/metadata may legitimately differ).
    std::string file_sha256;
};

namespace {

/// SHA-256 of a byte range, hex-encoded. The digest length comes from
/// libavutil (AV_HASH_MAX_SIZE is only an upper bound), so the reported
/// string is a true 64-character SHA-256 rather than a zero-padded buffer.
std::string sha256_hex(const std::uint8_t* data, std::size_t size) {
    AVHashContext* context = nullptr;
    if (av_hash_alloc(&context, "SHA256") < 0) return {};
    const int digest_size = av_hash_get_size(context);
    av_hash_init(context);
    if (data && size > 0) av_hash_update(context, data, size);
    std::array<std::uint8_t, AV_HASH_MAX_SIZE> digest{};
    av_hash_final(context, digest.data());
    av_hash_freep(&context);
    if (digest_size <= 0) return {};
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(digest_size) * 2);
    for (int index = 0; index < digest_size; ++index) {
        out.push_back(kHex[digest[index] >> 4]);
        out.push_back(kHex[digest[index] & 0x0f]);
    }
    return out;
}

/// Stream the whole file through SHA-256. The strongest signal the gate
/// reports, and the only one that is reported separately rather than asserted:
/// a container may be semantically identical with a different layout.
std::string file_sha256(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    AVHashContext* context = nullptr;
    if (av_hash_alloc(&context, "SHA256") < 0) return {};
    const int digest_size = av_hash_get_size(context);
    av_hash_init(context);
    std::array<char, 1 << 16> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = input.gcount();
        if (read > 0) {
            av_hash_update(context,
                           reinterpret_cast<const std::uint8_t*>(buffer.data()),
                           static_cast<std::size_t>(read));
        }
    }
    std::array<std::uint8_t, AV_HASH_MAX_SIZE> digest{};
    av_hash_final(context, digest.data());
    av_hash_freep(&context);
    if (digest_size <= 0) return {};
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(digest_size) * 2);
    for (int index = 0; index < digest_size; ++index) {
        out.push_back(kHex[digest[index] >> 4]);
        out.push_back(kHex[digest[index] & 0x0f]);
    }
    return out;
}

} // namespace

ContainerFacts probe_container(const std::string& path) {
    ContainerFacts facts;
    facts.file_sha256 = file_sha256(path);

    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
        return facts;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        return facts;
    }
    if (format->duration != AV_NOPTS_VALUE) facts.duration_us = format->duration;

    int video_index = -1;
    int audio_index = -1;
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
        const auto type = format->streams[index]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && video_index < 0) {
            video_index = static_cast<int>(index);
        }
        if (type == AVMEDIA_TYPE_AUDIO && audio_index < 0) {
            audio_index = static_cast<int>(index);
        }
    }
    if (video_index < 0) {
        avformat_close_input(&format);
        return facts;
    }

    {
        const auto* params = format->streams[video_index]->codecpar;
        facts.video.codec_id = params->codec_id;
        facts.video.profile = params->profile;
        facts.video.level = params->level;
        facts.video.pixel_format = params->format;
        facts.video.width = params->width;
        facts.video.height = params->height;
        facts.video.extradata_sha256 = sha256_hex(
            params->extradata, static_cast<std::size_t>(params->extradata_size));
    }
    if (audio_index >= 0) {
        const auto* params = format->streams[audio_index]->codecpar;
        facts.audio.present = true;
        facts.audio.codec_id = params->codec_id;
        facts.audio.sample_rate = params->sample_rate;
        facts.audio.channels = params->channels;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        avformat_close_input(&format);
        return facts;
    }
    bool have_previous_dts = false;
    std::int64_t previous_dts = 0;
    while (av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == video_index) {
            PacketFacts entry;
            entry.pts = packet->pts;
            entry.dts = packet->dts;
            entry.duration = packet->duration;
            entry.flags = packet->flags;
            entry.payload_sha256 = sha256_hex(
                packet->data, static_cast<std::size_t>(packet->size));
            if (have_previous_dts && packet->dts != AV_NOPTS_VALUE &&
                packet->dts < previous_dts) {
                facts.video.dts_monotonic = false;
            }
            if (packet->dts != AV_NOPTS_VALUE) {
                previous_dts = packet->dts;
                have_previous_dts = true;
            }
            facts.video.packets.push_back(std::move(entry));
        } else if (packet->stream_index == audio_index) {
            ++facts.audio.packet_count;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);

    // Decodability is part of the contract: output that cannot be decoded to
    // the expected frame count is not a valid parity candidate.
    const auto* video_params = format->streams[video_index]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(video_params->codec_id);
    AVCodecContext* context = decoder ? avcodec_alloc_context3(decoder) : nullptr;
    if (context && avcodec_parameters_to_context(context, video_params) >= 0 &&
        avcodec_open2(context, decoder, nullptr) >= 0) {
        AVFrame* frame = av_frame_alloc();
        AVPacket* decode_packet = av_packet_alloc();
        if (frame && decode_packet &&
            av_seek_frame(format, video_index, 0, AVSEEK_FLAG_BACKWARD) >= 0) {
            bool failed = false;
            const auto drain = [&]() {
                for (;;) {
                    const int ret = avcodec_receive_frame(context, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
                    if (ret < 0) return false;
                    ++facts.decoded_video_frames;
                    av_frame_unref(frame);
                }
            };
            while (av_read_frame(format, decode_packet) >= 0) {
                if (decode_packet->stream_index == video_index) {
                    if (avcodec_send_packet(context, decode_packet) < 0) {
                        failed = true;
                    } else if (!drain()) {
                        failed = true;
                    }
                }
                av_packet_unref(decode_packet);
                if (failed) break;
            }
            if (!failed && avcodec_send_packet(context, nullptr) >= 0) {
                failed = !drain();
            }
            facts.decoded_ok = !failed;
        }
        av_packet_free(&decode_packet);
        av_frame_free(&frame);
    }
    if (context) avcodec_free_context(&context);
    avformat_close_input(&format);

    facts.valid = true;
    return facts;
}

std::vector<std::string> compare_containers(const ContainerFacts& reference,
                                            const ContainerFacts& candidate) {
    std::vector<std::string> diffs;
    const auto note = [&](std::string message) {
        diffs.push_back(std::move(message));
    };

    if (!reference.valid || !candidate.valid) {
        note("container facts unavailable (unreadable or video-less container)");
        return diffs;
    }

    // ── Codec facts ────────────────────────────────────────────────────────
    if (reference.video.codec_id != candidate.video.codec_id) {
        note("video codec id differs");
    }
    if (reference.video.profile != candidate.video.profile) {
        note("video profile differs");
    }
    if (reference.video.level != candidate.video.level) {
        note("video level differs");
    }
    if (reference.video.width != candidate.video.width ||
        reference.video.height != candidate.video.height) {
        note("video dimensions differ");
    }
    if (reference.video.pixel_format != candidate.video.pixel_format) {
        note("video pixel format differs");
    }
    if (reference.video.extradata_sha256 != candidate.video.extradata_sha256) {
        note("video codec configuration record (SPS/PPS) differs");
    }

    // ── Packets: copied packets must be byte-identical ─────────────────────
    if (reference.video.packets.size() != candidate.video.packets.size()) {
        note("video packet count differs (" +
             std::to_string(reference.video.packets.size()) + " vs " +
             std::to_string(candidate.video.packets.size()) + ")");
    } else {
        for (std::size_t index = 0; index < reference.video.packets.size(); ++index) {
            const auto& expected = reference.video.packets[index];
            const auto& actual = candidate.video.packets[index];
            if (expected.payload_sha256 != actual.payload_sha256) {
                note("video packet " + std::to_string(index) +
                     " payload differs (copied packets must be byte-identical)");
                break;
            }
            if (expected.pts != actual.pts || expected.dts != actual.dts ||
                expected.duration != actual.duration ||
                expected.flags != actual.flags) {
                note("video packet " + std::to_string(index) +
                     " timing/flags differ");
                break;
            }
        }
    }
    if (reference.video.dts_monotonic != candidate.video.dts_monotonic) {
        note("video DTS monotonicity differs");
    }

    // ── Container envelope ─────────────────────────────────────────────────
    if (reference.duration_us != candidate.duration_us) {
        note("container duration differs");
    }
    if (reference.decoded_video_frames != candidate.decoded_video_frames) {
        note("decoded video frame count differs");
    }
    if (reference.decoded_ok != candidate.decoded_ok) {
        note("decodability differs");
    }

    // ── Audio ──────────────────────────────────────────────────────────────
    if (reference.audio.present != candidate.audio.present) {
        note("audio stream presence differs");
    } else if (reference.audio.present) {
        if (reference.audio.codec_id != candidate.audio.codec_id) {
            note("audio codec id differs");
        }
        if (reference.audio.sample_rate != candidate.audio.sample_rate) {
            note("audio sample rate differs");
        }
        if (reference.audio.channels != candidate.audio.channels) {
            note("audio channel count differs");
        }
        if (reference.audio.packet_count != candidate.audio.packet_count) {
            note("audio packet count differs");
        }
    }
    return diffs;
}

} // namespace chronon3d::cli::test_gop_parity

TEST_CASE("BoundaryReencode parity comparator accepts an identical container") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto base = std::filesystem::temp_directory_path() /
        "chronon3d_parity_base.mp4";
    std::error_code cleanup_error;
    std::filesystem::remove(base, cleanup_error);
    const std::string command =
        "ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i testsrc2=size=64x64:rate=10:duration=2 "
        "-f lavfi -i sine=frequency=440:duration=2 "
        "-c:v libx264 -preset ultrafast -pix_fmt yuv420p "
        "-g 10 -keyint_min 10 -sc_threshold 0 -c:a aac -shortest " + base.string();
    if (std::system(command.c_str()) != 0) {
        MESSAGE("Skipping — ffmpeg cannot create the parity fixture");
        std::filesystem::remove(base, cleanup_error);
        return;
    }

    const auto reference = chronon3d::cli::test_gop_parity::probe_container(base.string());
    REQUIRE(reference.valid);
    CHECK(reference.video.packets.size() == 20);
    CHECK(reference.video.codec_id == AV_CODEC_ID_H264);
    CHECK(reference.audio.present);
    CHECK(reference.decoded_ok);
    CHECK(reference.decoded_video_frames == 20);
    CHECK(reference.duration_us > 0);
    CHECK_FALSE(reference.file_sha256.empty());

    // A container compared with itself has no semantic difference.
    CHECK(chronon3d::cli::test_gop_parity::compare_containers(reference, reference).empty());

    std::filesystem::remove(base, cleanup_error);
}

TEST_CASE("P0-5D: source-declared direct mux matches the copy oracle") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto dir = std::filesystem::temp_directory_path() /
        "chronon3d_direct_mux_parity";
    std::error_code cleanup_error;
    std::filesystem::remove_all(dir, cleanup_error);
    std::filesystem::create_directories(dir, cleanup_error);

    const auto source = dir / "source.mp4";
    const auto oracle_path = dir / "oracle.mp4";
    const auto direct_path = dir / "direct.mp4";
    REQUIRE(std::system(("ffmpeg -hide_banner -loglevel error -y "
        "-f lavfi -i testsrc2=size=64x64:rate=10:duration=3 "
        "-f lavfi -i sine=frequency=440:duration=3 "
        "-c:v libx264 -preset ultrafast -pix_fmt yuv420p "
        "-g 10 -keyint_min 10 -sc_threshold 0 -c:a aac -shortest " +
        source.string()).c_str()) == 0);

    // The scheduled window is what the executor is asked to produce. It is
    // expressed here in seconds only because this test drives the CLI's
    // second-based fixture; the SOURCE-TICK form below is the authority the
    // work units and the audio submission actually speak.
    constexpr double kWindowStartSeconds = 0.0;
    constexpr double kWindowEndSeconds = 3.0;

    // Schedule from the REAL preflight — the same plans the executor consumes.
    const auto remux_target =
        chronon3d::cli::make_remux_target_contract(source.string());
    REQUIRE(remux_target.has_value());
    const auto analysis = chronon3d::cli::inspect_gop_source(
        source.string(), *remux_target, kWindowStartSeconds,
        kWindowEndSeconds, /*content_changed=*/false);
    REQUIRE(analysis.has_value());
    REQUIRE(analysis->all_copy_eligible);
    const double tick_seconds =
        static_cast<double>(analysis->source_time_base_num) /
        static_cast<double>(analysis->source_time_base_den);
    REQUIRE(tick_seconds > 0.0);
    const chronon3d::media::TimestampRange schedule_window{
        static_cast<std::int64_t>(std::llround(kWindowStartSeconds / tick_seconds)),
        static_cast<std::int64_t>(std::llround(kWindowEndSeconds / tick_seconds)),
        true};
    const double end_seconds = kWindowEndSeconds;

    std::vector<chronon3d::media::GopWorkUnit> units;
    for (const auto& plan : analysis->plans) {
        units.push_back(chronon3d::media::GopWorkUnit{
            .ordinal = plan.ordinal,
            .first_pts = plan.first_pts,
            .last_pts = plan.last_pts,
            .first_dts = plan.first_dts,
            .last_dts = plan.last_dts,
            .kind = chronon3d::media::GopWorkKind::CopyPackets,
            .source_packets = chronon3d::media::TimestampRange{
                plan.first_dts, plan.last_dts, true},
            .compatibility = plan.compatibility});
    }
    REQUIRE(units.size() == analysis->plans.size());

    // ── Path B: one caller-owned mux, declared from the SOURCE ────────────
    chronon3d::media::MuxSession direct_mux;
    const auto declaration = chronon3d::cli::open_source_declared_mux(
        source.string(), direct_path.string(), direct_mux, /*include_audio=*/true);
    REQUIRE(declaration.has_value());
    std::uint64_t submitted_video = 0;
    for (const auto& unit : units) {
        const auto submitted = chronon3d::cli::submit_gop_source_packets(
            source.string(), unit, direct_mux);
        REQUIRE(submitted.has_value());
        submitted_video += submitted->video_packets;
    }
    // Audio covers the SCHEDULED window, in source video ticks — one call,
    // exactly once, converted to the audio time base by the container.
    const auto submitted_audio = chronon3d::cli::submit_source_audio(
        source.string(), direct_mux, schedule_window);
    REQUIRE(submitted_audio.has_value());
    // Audio is written exactly once, by the caller.
    CHECK(submitted_audio->audio_packets > 0);
    REQUIRE(direct_mux.finalize());

    // ── Path A: the oracle (whole-clip copy through its own container) ────
    const auto oracle = chronon3d::cli::copy_gop_source(
        source.string(), oracle_path.string(), 0.0, end_seconds);
    REQUIRE(oracle.has_value());
    CHECK(submitted_video == oracle->video_packets);

    // ── Parity gate ──────────────────────────────────────────────────────
    const auto expected =
        chronon3d::cli::test_gop_parity::probe_container(oracle_path.string());
    const auto actual =
        chronon3d::cli::test_gop_parity::probe_container(direct_path.string());
    REQUIRE(expected.valid);
    REQUIRE(actual.valid);
    // The whole-file digests are reported, never asserted: a container may be
    // semantically identical with a different layout/metadata. Packet byte
    // identity above is the absolute criterion for the copied GOPs.
    MESSAGE("oracle whole-file sha256: " << expected.file_sha256);
    MESSAGE("direct whole-file sha256: " << actual.file_sha256);
    CHECK(expected.video.packets.size() > 0);
    CHECK(expected.video.packets.size() == actual.video.packets.size());

    const auto diffs =
        chronon3d::cli::test_gop_parity::compare_containers(expected, actual);
    for (const auto& diff : diffs) MESSAGE("parity diff: " << diff);
    CHECK(diffs.empty());

    std::filesystem::remove_all(dir, cleanup_error);
}

TEST_CASE("BoundaryReencode parity comparator reports each difference class") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto dir = std::filesystem::temp_directory_path() /
        "chronon3d_parity_mutations";
    std::error_code cleanup_error;
    std::filesystem::remove_all(dir, cleanup_error);
    std::filesystem::create_directories(dir, cleanup_error);

    const auto base = dir / "base.mp4";
    const auto shorter = dir / "shorter.mp4";
    const auto reencoded = dir / "reencoded.mp4";
    const auto silent = dir / "silent.mp4";
    const std::string source =
        "-f lavfi -i testsrc2=size=64x64:rate=10:duration=2 "
        "-f lavfi -i sine=frequency=440:duration=2 ";
    const auto run = [](const std::string& command) {
        return std::system(command.c_str()) == 0;
    };

    REQUIRE(run("ffmpeg -hide_banner -loglevel error -y " + source +
                "-c:v libx264 -preset ultrafast -pix_fmt yuv420p "
                "-g 10 -keyint_min 10 -sc_threshold 0 -c:a aac -shortest " +
                base.string()));
    // Same bitstream, narrower window: copied packets stay byte-identical.
    REQUIRE(run("ffmpeg -hide_banner -loglevel error -y -i " + base.string() +
                " -t 1.0 -c copy " + shorter.string()));
    // Re-encoded: every packet payload changes.
    REQUIRE(run("ffmpeg -hide_banner -loglevel error -y " + source +
                "-c:v libx264 -preset veryfast -crf 34 -pix_fmt yuv420p "
                "-g 10 -keyint_min 10 -sc_threshold 0 -c:a aac -shortest " +
                reencoded.string()));
    // Audio removed.
    REQUIRE(run("ffmpeg -hide_banner -loglevel error -y -i " + base.string() +
                " -an -c:v copy " + silent.string()));

    const auto reference = chronon3d::cli::test_gop_parity::probe_container(base.string());
    REQUIRE(reference.valid);

    const auto trimmed = chronon3d::cli::test_gop_parity::probe_container(shorter.string());
    REQUIRE(trimmed.valid);
    CHECK_FALSE(chronon3d::cli::test_gop_parity::compare_containers(reference, trimmed).empty());

    const auto recoded = chronon3d::cli::test_gop_parity::probe_container(reencoded.string());
    REQUIRE(recoded.valid);
    CHECK_FALSE(chronon3d::cli::test_gop_parity::compare_containers(reference, recoded).empty());

    const auto muted = chronon3d::cli::test_gop_parity::probe_container(silent.string());
    REQUIRE(muted.valid);
    CHECK_FALSE(chronon3d::cli::test_gop_parity::compare_containers(reference, muted).empty());

    std::filesystem::remove_all(dir, cleanup_error);
}

#endif // CHRONON3D_ENABLE_NATIVE_FFMPEG
