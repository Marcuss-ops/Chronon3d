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
#include <utility>
#include <vector>

namespace chronon3d::cli::test_gop_parity {

/// One authoring shape the direct-mux executor must reproduce. These are the
/// schedule shapes the legacy segment-file executor is the oracle for.
struct GopParityCase {
    std::string_view name;
    /// The planned GOP kinds the schedule MUST produce, in source order. The
    /// corpus is the gate's input AND its expectation: a shape whose schedule
    /// silently changes is a different shape.
    std::string_view schedule;
    /// Number of disjoint modified blocks (content edits) in the timeline.
    std::size_t edited_blocks{0};
    /// The requested window is narrower than the source (a real trim).
    bool trim{false};
    /// Requested window, in seconds, against the shared 14 s fixture.
    double window_start_seconds{0.0};
    double window_end_seconds{0.0};
    /// Content edits, in seconds, against the same fixture.
    std::vector<std::pair<double, double>> edits{};

    [[nodiscard]] std::string expected_schedule() const {
        std::string out(schedule);
        out.push_back(',');
        return out;
    }
};

/// The frozen corpus. Growing it is fine; removing a shape silently shrinks
/// the demolition gate, which is why the test pins every name and the aligned
/// schedule each one must produce on the shared 14 s fixture (7 GOPs of the
/// contract's 2 s cadence).
[[nodiscard]] inline const std::vector<GopParityCase>& gop_parity_corpus() {
    static const std::vector<GopParityCase> corpus{
        {"copy_reencode_copy", "COPY,REENCODE,COPY", 1, false, 0.0, 6.0,
         {{2.0, 2.1}}},
        {"reencode_copy", "REENCODE,COPY", 1, false, 2.0, 6.0, {{2.0, 2.1}}},
        {"copy_reencode", "COPY,REENCODE", 1, false, 0.0, 4.0, {{2.0, 2.1}}},
        {"two_separate_modified_blocks",
         "COPY,REENCODE,COPY,REENCODE,COPY", 2, false, 0.0, 10.0,
         {{2.0, 2.1}, {6.0, 6.1}}},
        {"trim_plus_content_edit", "COPY,REENCODE,COPY", 1, true, 4.0, 10.0,
         {{6.0, 6.1}}},
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
        CHECK(entry.edited_blocks >= 1);
        CHECK(entry.edits.size() == entry.edited_blocks);
        CHECK(entry.window_end_seconds > entry.window_start_seconds);
    }
    // The two shapes that must not collapse into each other: a single modified
    // block and two SEPARATE ones, plus the trimmed window.
    const auto entry_for = [&](std::string_view name)
        -> const chronon3d::cli::test_gop_parity::GopParityCase& {
        for (const auto& entry : corpus) {
            if (entry.name == name) return entry;
        }
        return corpus.front();
    };
    CHECK(entry_for("two_separate_modified_blocks").edited_blocks == 2);
    CHECK(entry_for("trim_plus_content_edit").trim);
    CHECK(entry_for("trim_plus_content_edit").window_start_seconds > 0.0);
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hash.h>
}

#include "utils/video/gop_smart_copy.hpp"
#include "utils/video/native_av_encoder.hpp"
// Full definition of MuxSession: the direct-mux test owns the session itself.
#include "chronon3d/media/video/packet_assembler.hpp"

namespace chronon3d::cli::test_gop_parity {

/// Encoder options for the hybrid parity fixture: the SAME configuration is
/// used for the direct lane's borrowed encoder and for the oracle's segment
/// encoder, so the re-encoded payloads are comparable at all.
[[nodiscard]] inline chronon3d::cli::FfmpegPipeOptions parity_encoder_options(
    const std::string& output_path) {
    chronon3d::cli::FfmpegPipeOptions options;
    options.width = 64;
    options.height = 64;
    options.fps_num = 10;
    options.fps_den = 1;
    options.codec = "libx264";
    options.hardware_encoder = "none";
    options.preset = "ultrafast";
    options.rate_control_mode = "crf";
    options.rate_control_mode_explicit = true;
    options.crf = 23;
    options.crf_explicit = true;
    options.output_path = output_path;
    return options;
}

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
    /// Audio stream time base and the PTS of every packet, so a window can be
    /// counted independently of any producer.
    int time_base_num{0};
    int time_base_den{1};
    std::vector<std::int64_t> packet_pts;
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
#if LIBAVCODEC_VERSION_MAJOR >= 59
        facts.audio.channels = params->ch_layout.nb_channels;
#else
        facts.audio.channels = params->channels;
#endif
        facts.audio.time_base_num = format->streams[audio_index]->time_base.num;
        facts.audio.time_base_den = format->streams[audio_index]->time_base.den;
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
            if (packet->pts != AV_NOPTS_VALUE) {
                facts.audio.packet_pts.push_back(packet->pts);
            }
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

// ── P0-5E/5F — COPY + REENCODE + COPY in ONE caller-owned container ──────
//
// The mechanism the production executor is built from, against the legacy
// segment-file mechanism as oracle (one temp MP4 per segment, then remux):
//
//   direct:  source-declared mux ← copy(GOP0) + borrowed encoder(GOP1 @ its
//            scheduled tick) + copy(GOP2) → finalize once
//   oracle:  copy_gop_source(0..1s) + encoder segment file +
//            copy_gop_source(2..3s) → assemble_segments()
//
// Both must agree packet-for-packet: the copied GOPs byte-identical, every
// packet identical in PTS/DTS/duration/flags, the same decodable frame count.
// This is the gate the demolition of the segment path (P0-5G) depended on.
TEST_CASE("P0-5E/5F: the direct hybrid mux matches the segment-file oracle") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto dir = std::filesystem::temp_directory_path() /
        "chronon3d_hybrid_direct_mux";
    std::error_code cleanup_error;
    std::filesystem::remove_all(dir, cleanup_error);
    std::filesystem::create_directories(dir, cleanup_error);

    // The compressed source is authored with the SAME encoder configuration
    // the re-encode lane will use. That is not a convenience: the splice gate
    // requires the encoder to REPRODUCE the declared stream, and the legacy
    // segment mechanism (the oracle below) requires the segments it assembles
    // to carry the same configuration record, so a foreign source could not
    // reach the boundary either way. 6 s at 10 fps with the contract's 2 s
    // keyframe cadence is exactly 3 GOPs.
    const auto source = dir / "source.mp4";
    {
        chronon3d::cli::NativeAvEncoder writer;
        REQUIRE(writer.open(
            chronon3d::cli::test_gop_parity::parity_encoder_options(source.string())));
        for (int index = 0; index < 60; ++index) {
            chronon3d::Framebuffer frame(64, 64);
            REQUIRE(writer.write_frame(frame));
        }
        REQUIRE(writer.close());
    }

    // The schedule comes from the REAL preflight, against the container
    // declaration the direct lane opens: the source's own stream.
    const auto declared = chronon3d::cli::make_remux_target_contract(source.string());
    REQUIRE(declared.has_value());
    const std::vector<chronon3d::media::ChangedInterval> edits{
        {2.0, 2.1, chronon3d::media::ChangedIntervalKind::Content}};
    const auto analysis = chronon3d::cli::inspect_gop_source(
        source.string(), *declared, 0.0, 6.0, /*content_changed=*/true, edits);
    REQUIRE(analysis.has_value());
    REQUIRE(analysis->plans.size() == 3);
    // Frozen corpus shape `copy_reencode_copy`.
    CHECK(analysis->plans[0].copy_packets());
    CHECK_FALSE(analysis->plans[1].copy_packets());
    CHECK(analysis->plans[2].copy_packets());
    REQUIRE(analysis->is_hybrid());
    // The container's zero is the schedule's own left edge (0 here: the window
    // starts at the source's start).
    const std::int64_t schedule_origin = analysis->plans.front().first_dts;

    const auto unit_for = [&](std::size_t index) {
        chronon3d::media::GopWorkUnit unit;
        unit.ordinal = index;
        unit.first_pts = analysis->plans[index].first_pts;
        unit.last_pts = analysis->plans[index].last_pts;
        unit.first_dts = analysis->plans[index].first_dts;
        unit.last_dts = analysis->plans[index].last_dts;
        unit.kind = analysis->plans[index].copy_packets()
            ? chronon3d::media::GopWorkKind::CopyPackets
            : chronon3d::media::GopWorkKind::Reencode;
        unit.source_packets = chronon3d::media::TimestampRange{
            analysis->plans[index].first_dts, analysis->plans[index].last_dts,
            true};
        unit.decode_range = chronon3d::media::TimestampRange{
            analysis->plans[index].decode_first_dts,
            analysis->plans[index].last_dts, true};
        unit.render_range = chronon3d::media::TimestampRange{
            analysis->plans[index].first_pts, analysis->plans[index].last_pts,
            true};
        unit.encode_range = unit.render_range;
        unit.force_idr_at_start = !unit.copies_packets();
        unit.compatibility = analysis->plans[index].compatibility;
        return unit;
    };
    // Frame count of the re-encoded unit, derived from its tick span exactly
    // the way the executor's exact rescale does (source ticks → frames).
    const std::int64_t tb_num = analysis->source_time_base_num;
    const std::int64_t tb_den = analysis->source_time_base_den;
    REQUIRE(tb_num > 0);
    REQUIRE(tb_den > 0);
    REQUIRE(tb_den > 0);
    const auto middle_frames = static_cast<int>(
        ((analysis->plans[1].last_pts - analysis->plans[1].first_pts) * tb_num *
         10) /
            (tb_den * 1) +
        1);
    REQUIRE(middle_frames > 0);
    MESSAGE("re-encoded unit frames: " << middle_frames << " (ticks "
            << analysis->plans[1].first_pts << "-"
            << analysis->plans[1].last_pts << " @ " << tb_num << "/" << tb_den
            << ")");

    // ── Path B: the direct mux (production mechanism) ────────────────────
    const auto direct_path = dir / "direct.mp4";
    std::uint64_t direct_video = 0;
    {
        chronon3d::media::MuxSession mux;
        const auto declaration = chronon3d::cli::open_source_declared_mux(
            source.string(), direct_path.string(), mux, /*include_audio=*/false);
        REQUIRE(declaration.has_value());

        const auto first = chronon3d::cli::submit_gop_source_packets(
            source.string(), unit_for(0), mux, schedule_origin);
        REQUIRE(first.has_value());
        direct_video += first->video_packets;

        // The re-encoded unit is a CHILD render writing into the caller's
        // container at the tick the schedule reserved for it, certified against
        // the stream the container was declared from (P0-5E).
        mux.set_video_origin(analysis->plans[1].first_pts - schedule_origin);
        {
            chronon3d::cli::NativeAvEncoder encoder;
            auto options = chronon3d::cli::test_gop_parity::parity_encoder_options(
                (dir / "unused.mp4").string());
            options.borrowed_mux = &mux;
            options.borrowed_declared_stream = *declared;
            REQUIRE(encoder.open(options));
            CHECK(encoder.borrows_caller_mux());
            for (int index = 0; index < middle_frames; ++index) {
                chronon3d::Framebuffer frame(64, 64);
                REQUIRE(encoder.write_frame(frame));
            }
            // close() flushes the encoder only: the container stays open for
            // the work units that follow (P0-5C), and it certifies that this
            // encoder reproduced the declared stream (otherwise a decoder
            // could not follow the splice).
            REQUIRE(encoder.close());
            CHECK(encoder.borrowed_stream_reproduced());
        }

        const auto last = chronon3d::cli::submit_gop_source_packets(
            source.string(), unit_for(2), mux, schedule_origin);
        REQUIRE(last.has_value());
        direct_video += last->video_packets;

        // The container is finalized exactly once, by its owner (P0-5F).
        REQUIRE(mux.finalize());
    }

    // ── Path A: the oracle (segment files + assemble_segments) ───────────
    const auto oracle_path = dir / "oracle.mp4";
    std::vector<std::string> segments;
    const auto copy_segment = [&](int index, double start_seconds,
                                  double end_seconds) {
        const auto segment = dir / ("segment-" + std::to_string(index) + ".mp4");
        REQUIRE(chronon3d::cli::copy_gop_source(
                    source.string(), segment.string(), start_seconds,
                    end_seconds)
                    .has_value());
        segments.push_back(segment.string());
    };
    copy_segment(0, 0.0, 2.0);
    // The middle segment is the same encoder configuration, writing into a
    // container of its own exactly like the legacy executor did.
    {
        const auto segment = dir / "segment-1.mp4";
        chronon3d::cli::NativeAvEncoder encoder;
        REQUIRE(encoder.open(
            chronon3d::cli::test_gop_parity::parity_encoder_options(
                segment.string())));
        for (int index = 0; index < middle_frames; ++index) {
            chronon3d::Framebuffer frame(64, 64);
            REQUIRE(encoder.write_frame(frame));
        }
        REQUIRE(encoder.close());
        segments.push_back(segment.string());
    }
    copy_segment(2, 4.0, 6.0);
    REQUIRE(segments.size() == 3);
    const auto assembled = chronon3d::media::assemble_segments(
        {segments, oracle_path.string()});
    MESSAGE("segment assembly: success=" << assembled.success
            << " reason=" << assembled.reason);
    REQUIRE(assembled.success);

    // ── Parity gate ──────────────────────────────────────────────────────
    const auto expected =
        chronon3d::cli::test_gop_parity::probe_container(oracle_path.string());
    const auto actual =
        chronon3d::cli::test_gop_parity::probe_container(direct_path.string());
    REQUIRE(expected.valid);
    REQUIRE(actual.valid);
    MESSAGE("oracle whole-file sha256: " << expected.file_sha256);
    MESSAGE("direct whole-file sha256: " << actual.file_sha256);
    // The copy units must cover exactly the copies the oracle produced; the
    // re-encoded unit adds its own frames on top.
    CHECK(direct_video + static_cast<std::uint64_t>(middle_frames) ==
          expected.video.packets.size());
    CHECK(actual.video.packets.size() == expected.video.packets.size());

    MESSAGE("direct copied packets=" << direct_video << ", re-encoded="
            << middle_frames << ", oracle packets="
            << expected.video.packets.size() << ", direct packets="
            << actual.video.packets.size() << ", oracle frames="
            << expected.decoded_video_frames << ", direct frames="
            << actual.decoded_video_frames << ", decoded ok: oracle="
            << expected.decoded_ok << " direct=" << actual.decoded_ok);
    const auto diffs =
        chronon3d::cli::test_gop_parity::compare_containers(expected, actual);
    for (const auto& diff : diffs) MESSAGE("parity diff: " << diff);
    CHECK(diffs.empty());

    // Placement, stated explicitly: the re-encoded unit sits exactly where the
    // schedule put it, and no packet restarts the container at zero.
    std::int64_t previous_pts = -1;
    for (const auto& packet : actual.video.packets) {
        CHECK(packet.pts > previous_pts);
        previous_pts = packet.pts;
    }
    REQUIRE(actual.video.packets.size() > static_cast<std::size_t>(middle_frames));
    CHECK(actual.video.packets[middle_frames].pts ==
          analysis->plans[1].first_pts - schedule_origin);

    if (std::getenv("CHRONON3D_KEEP_PARITY_FIXTURES") == nullptr) {
        std::filesystem::remove_all(dir, cleanup_error);
    } else {
        MESSAGE("fixtures kept in " << dir.string());
    }
}

// ── P0-5B/5G — the whole frozen corpus, direct lane vs the segment oracle ──
//
// Every shape in `gop_parity_corpus()` is authored on ONE 14 s fixture and run
// twice: once through the single caller-owned MuxSession (copies + borrowed
// encoders + audio once + one finalize) and once through the legacy
// segment-file mechanism (one MP4 per unit, then assemble_segments). The gate
// is the legacy output, so the demolition of that mechanism (P0-5G) is only
// legal while every shape stays packet-for-packet identical to it.
//
// Audio is part of the fixture on purpose: P0-5F is exactly the claim that the
// track is submitted ONCE regardless of how many work units ran, and that the
// container's zero applies to video and audio together.
TEST_CASE("P0-5B: every frozen hybrid shape matches the segment-file oracle") {
    if (std::system("ffmpeg -hide_banner -version >/dev/null 2>&1") != 0) {
        MESSAGE("Skipping — ffmpeg executable is not available");
        return;
    }
    const auto dir = std::filesystem::temp_directory_path() /
        "chronon3d_hybrid_corpus";
    std::error_code cleanup_error;
    std::filesystem::remove_all(dir, cleanup_error);
    std::filesystem::create_directories(dir, cleanup_error);

    // ── Fixture: 7 GOPs of the contract's 2 s cadence, authored by the SAME
    // encoder configuration the splice lane re-encodes with (the gate requires
    // the encoder to reproduce the declared stream), plus an audio track added
    // by remuxing the video stream verbatim.
    const auto video_source = dir / "source-video.mp4";
    {
        chronon3d::cli::NativeAvEncoder writer;
        REQUIRE(writer.open(
            chronon3d::cli::test_gop_parity::parity_encoder_options(
                video_source.string())));
        for (int index = 0; index < 140; ++index) {
            chronon3d::Framebuffer frame(64, 64);
            REQUIRE(writer.write_frame(frame));
        }
        REQUIRE(writer.close());
    }
    const auto source = video_source;

    // The audio-bearing variant exists for ONE claim (P0-5F): the track is
    // submitted exactly once for the whole schedule. It is not used for the
    // oracle comparison, because the legacy mechanism assembles per-segment
    // files and a re-encoded segment has no audio of its own (an encoder does
    // not invent a track) — that is a limitation of the mechanism, not of the
    // direct lane.
    const auto source_audio = dir / "source-with-audio.mp4";
    REQUIRE(std::system(("ffmpeg -hide_banner -loglevel error -y -i " +
                         source.string() +
                         " -f lavfi -i sine=frequency=440:duration=14 "
                         "-c:v copy -c:a aac -shortest " +
                         source_audio.string()).c_str()) == 0);

    const auto declared = chronon3d::cli::make_remux_target_contract(source.string());
    REQUIRE(declared.has_value());
    const auto source_facts =
        chronon3d::cli::test_gop_parity::probe_container(source.string());
    REQUIRE(source_facts.valid);
    REQUIRE_FALSE(source_facts.audio.present);
    REQUIRE(source_facts.video.packets.size() == 140);

    for (const auto& entry :
         chronon3d::cli::test_gop_parity::gop_parity_corpus()) {
        INFO("corpus case: " << entry.name);
        std::vector<chronon3d::media::ChangedInterval> edits;
        for (const auto& edit : entry.edits) {
            edits.push_back(chronon3d::media::ChangedInterval{
                edit.first, edit.second,
                chronon3d::media::ChangedIntervalKind::Content});
        }
        const auto analysis = chronon3d::cli::inspect_gop_source(
            source.string(), *declared, entry.window_start_seconds,
            entry.window_end_seconds, /*content_changed=*/!edits.empty(), edits);
        REQUIRE(analysis.has_value());
        REQUIRE(analysis->valid());
        REQUIRE(analysis->is_hybrid());

        const std::int64_t tb_num = analysis->source_time_base_num;
        const std::int64_t tb_den = analysis->source_time_base_den;
        REQUIRE(tb_num > 0);
        REQUIRE(tb_den > 0);

        // The preflight plan covers the whole source; the window selects the
        // units it needs. Same selection authority the production executor
        // uses, so the gate exercises the real window semantics.
        const auto plan =
            chronon3d::cli::build_gop_plan_from_analysis(*analysis,
                                                         /*splice_safe=*/true);
        const std::int64_t request_start = static_cast<std::int64_t>(
            std::llround(entry.window_start_seconds * tb_den / tb_num));
        const std::int64_t request_end = static_cast<std::int64_t>(
            std::llround(entry.window_end_seconds * tb_den / tb_num));
        const auto selected = chronon3d::media::gop_units_in_window(
            plan, request_start, request_end);
        REQUIRE_FALSE(selected.empty());
        // The window's own units, in schedule order — the same view the
        // executor renders from.
        const auto selected_schedule = [&]() {
            std::vector<chronon3d::cli::GopPlan> units;
            for (const auto index : selected) {
                units.push_back(analysis->plans[index]);
            }
            return units;
        };
        const std::int64_t schedule_origin = request_start;
        // The frames a re-encoded unit must produce, from its own tick span,
        // clipped to the window on the left exactly like the executor does.
        // The unit's frames are its HALF-OPEN tick span [start, right edge)
        // rescaled exactly: the exported frame the schedule reserved for this
        // unit, never one more (an extra frame would overlap the next unit).
        const auto unit_frames = [&](std::size_t index) {
            const auto schedule = selected_schedule();
            const std::int64_t unit_start = std::max<std::int64_t>(
                schedule[index].first_pts, schedule_origin);
            const std::int64_t unit_end = index + 1 < schedule.size()
                ? schedule[index + 1].first_pts
                : request_end;
            return static_cast<int>(
                ((unit_end - unit_start) * tb_num * 10) / (tb_den * 1));
        };

        // ── The schedule THIS window needs is pinned ────────────────────────
        std::string planned;
        for (const auto& unit : selected_schedule()) {
            planned += unit.copy_packets() ? "COPY" : "REENCODE";
            planned.push_back(',');
        }
        MESSAGE("planned schedule: " << planned << " (expected "
                << entry.expected_schedule() << ")");
        CHECK(planned == entry.expected_schedule());

        // ── Direct lane: ONE caller-owned container (video only here; audio
        // is asserted once, below, on its own fixture) ────────────────────
        const auto direct_path = dir / (std::string(entry.name) + "-direct.mp4");
        const auto oracle_path = dir / (std::string(entry.name) + "-oracle.mp4");
        std::vector<std::string> segments;
        std::uint64_t direct_copies = 0;
        std::uint64_t direct_reencoded = 0;
        {
            chronon3d::media::MuxSession mux;
            const auto declaration = chronon3d::cli::open_source_declared_mux(
                source.string(), direct_path.string(), mux,
                /*include_audio=*/false);
            REQUIRE(declaration.has_value());
            const auto units = selected_schedule();
            for (std::size_t index = 0; index < units.size(); ++index) {
                const auto& plan = units[index];
                if (plan.copy_packets()) {
                    const auto unit = chronon3d::media::GopWorkUnit{
                        .ordinal = index,
                        .first_pts = plan.first_pts,
                        .last_pts = plan.last_pts,
                        .first_dts = plan.first_dts,
                        .last_dts = plan.last_dts,
                        .kind = chronon3d::media::GopWorkKind::CopyPackets,
                        .source_packets = chronon3d::media::TimestampRange{
                            plan.first_dts, plan.last_dts, true},
                        .compatibility = plan.compatibility};
                    const auto submitted =
                        chronon3d::cli::submit_gop_source_packets(
                            source.string(), unit, mux, schedule_origin);
                    REQUIRE(submitted.has_value());
                    direct_copies += submitted->video_packets;
                    continue;
                }
                mux.set_video_origin(plan.first_pts - schedule_origin);
                chronon3d::cli::NativeAvEncoder encoder;
                auto options =
                    chronon3d::cli::test_gop_parity::parity_encoder_options(
                        (dir / "unused.mp4").string());
                options.borrowed_mux = &mux;
                options.borrowed_declared_stream = *declared;
                REQUIRE(encoder.open(options));
                const int frames = unit_frames(index);
                REQUIRE(frames > 0);
                for (int frame = 0; frame < frames; ++frame) {
                    chronon3d::Framebuffer rendered(64, 64);
                    REQUIRE(encoder.write_frame(rendered));
                }
                REQUIRE(encoder.close());
                CHECK(encoder.borrowed_stream_reproduced());
                direct_reencoded += static_cast<std::uint64_t>(frames);
            }
            REQUIRE(mux.finalize());
        }

        // ── Oracle lane: one segment per unit, then assemble ──────────────
        const auto oracle_units = selected_schedule();
        for (std::size_t index = 0; index < oracle_units.size(); ++index) {
            const auto& plan = oracle_units[index];
            const auto segment = dir / (std::string(entry.name) + "-segment-" +
                                        std::to_string(index) + ".mp4");
            // The legacy mechanism works on the SOURCE's own timeline (it
            // normalizes each segment to zero itself), so it is given absolute
            // source seconds — the window origin is this lane's business.
            const double start_seconds =
                static_cast<double>(plan.first_dts) * tb_num / tb_den;
            const double end_seconds =
                static_cast<double>(plan.last_dts + 1) * tb_num / tb_den;
            if (plan.copy_packets()) {
                REQUIRE(chronon3d::cli::copy_gop_source(
                            source.string(), segment.string(), start_seconds,
                            end_seconds)
                            .has_value());
                segments.push_back(segment.string());
                continue;
            }
            chronon3d::cli::NativeAvEncoder encoder;
            REQUIRE(encoder.open(
                chronon3d::cli::test_gop_parity::parity_encoder_options(
                    segment.string())));
            const int frames = unit_frames(index);
            for (int frame = 0; frame < frames; ++frame) {
                chronon3d::Framebuffer rendered(64, 64);
                REQUIRE(encoder.write_frame(rendered));
            }
            REQUIRE(encoder.close());
            segments.push_back(segment.string());
        }
        const auto assembled = chronon3d::media::assemble_segments(
            {segments, oracle_path.string()});
        MESSAGE("assembly: success=" << assembled.success
                << " reason=" << assembled.reason);
        REQUIRE(assembled.success);

        // ── The gate ──────────────────────────────────────────────────────
        const auto expected =
            chronon3d::cli::test_gop_parity::probe_container(oracle_path.string());
        const auto actual = chronon3d::cli::test_gop_parity::probe_container(
            direct_path.string());
        REQUIRE(expected.valid);
        REQUIRE(actual.valid);
        MESSAGE("copies=" << direct_copies << " reencoded=" << direct_reencoded
                << " direct packets=" << actual.video.packets.size()
                << " oracle packets=" << expected.video.packets.size()
                << " window=" << schedule_origin << "-" << request_end);
        CHECK(direct_copies + direct_reencoded ==
              expected.video.packets.size());
        CHECK(actual.video.packets.size() == expected.video.packets.size());
        const auto diffs =
            chronon3d::cli::test_gop_parity::compare_containers(expected, actual);
        for (const auto& diff : diffs) MESSAGE("parity diff: " << diff);
        if (!diffs.empty()) {
            // Pinpoint the first differing packets: index, payload, PTS, flags.
            for (std::size_t index = 0;
                 index < expected.video.packets.size() &&
                 index < actual.video.packets.size() && index < 64;
                 ++index) {
                const auto& lhs = expected.video.packets[index];
                const auto& rhs = actual.video.packets[index];
                if (lhs.payload_sha256 == rhs.payload_sha256 &&
                    lhs.pts == rhs.pts && lhs.flags == rhs.flags) {
                    continue;
                }
                MESSAGE("packet " << index << " oracle "
                        << lhs.payload_sha256.substr(0, 10) << " pts=" << lhs.pts
                        << " flags=" << lhs.flags << " | direct "
                        << rhs.payload_sha256.substr(0, 10) << " pts=" << rhs.pts
                        << " flags=" << rhs.flags);
            }
            MESSAGE("frames: oracle=" << expected.decoded_video_frames
                    << " direct=" << actual.decoded_video_frames
                    << " duration: oracle=" << expected.duration_us
                    << " direct=" << actual.duration_us);
        }
        CHECK(actual.decoded_video_frames == actual.video.packets.size());
        CHECK(diffs.empty());
        CHECK(actual.decoded_ok);

        // ── Copied packets: byte identity against the SOURCE itself ───────
        // Stronger than parity with the oracle: every copied unit must carry
        // the source's own bytes, at the container tick the schedule assigned.
        std::size_t output_index = 0;
        for (std::size_t index = 0; index < oracle_units.size(); ++index) {
            const auto& plan = oracle_units[index];
            if (!plan.copy_packets()) {
                output_index += static_cast<std::size_t>(unit_frames(index));
                continue;
            }
            std::size_t compared = 0;
            for (const auto& packet : source_facts.video.packets) {
                if (packet.pts < plan.first_pts || packet.pts > plan.last_pts) {
                    continue;
                }
                REQUIRE(output_index < actual.video.packets.size());
                CHECK(actual.video.packets[output_index].payload_sha256 ==
                      packet.payload_sha256);
                CHECK(actual.video.packets[output_index].pts ==
                      packet.pts - schedule_origin);
                CHECK(actual.video.packets[output_index].flags == packet.flags);
                ++output_index;
                ++compared;
            }
            CHECK(compared > 0);
        }

    }

    // ── P0-5F — audio is submitted ONCE, for the whole schedule ───────────
    // Run on the audio-bearing fixture, on the shape with three units: a
    // per-unit submission (the defect this asserts against) would multiply the
    // count, and a missing window origin would offset the track.
    {
        const auto& entry = chronon3d::cli::test_gop_parity::gop_parity_corpus()[0];
        INFO("audio case: " << entry.name);
        const auto declared_audio =
            chronon3d::cli::make_remux_target_contract(source_audio.string());
        REQUIRE(declared_audio.has_value());
        const auto audio_facts =
            chronon3d::cli::test_gop_parity::probe_container(source_audio.string());
        REQUIRE(audio_facts.valid);
        REQUIRE(audio_facts.audio.present);

        std::vector<chronon3d::media::ChangedInterval> edits;
        for (const auto& edit : entry.edits) {
            edits.push_back(chronon3d::media::ChangedInterval{
                edit.first, edit.second,
                chronon3d::media::ChangedIntervalKind::Content});
        }
        const auto analysis = chronon3d::cli::inspect_gop_source(
            source_audio.string(), *declared_audio, entry.window_start_seconds,
            entry.window_end_seconds, /*content_changed=*/true, edits);
        REQUIRE(analysis.has_value());
        const std::int64_t tb_num = analysis->source_time_base_num;
        const std::int64_t tb_den = analysis->source_time_base_den;
        const std::int64_t schedule_origin = static_cast<std::int64_t>(
            std::llround(entry.window_start_seconds * tb_den / tb_num));
        const std::int64_t request_end = static_cast<std::int64_t>(
            std::llround(entry.window_end_seconds * tb_den / tb_num));
        const auto plan = chronon3d::cli::build_gop_plan_from_analysis(
            *analysis, /*splice_safe=*/true);
        const auto selected = chronon3d::media::gop_units_in_window(
            plan, schedule_origin, request_end);
        REQUIRE_FALSE(selected.empty());
        std::vector<chronon3d::cli::GopPlan> units;
        for (const auto index : selected) units.push_back(analysis->plans[index]);
        const auto frames_for = [&](std::size_t index) {
            const std::int64_t unit_start = std::max<std::int64_t>(
                units[index].first_pts, schedule_origin);
            const std::int64_t unit_end = index + 1 < units.size()
                ? units[index + 1].first_pts
                : request_end;
            return static_cast<int>(
                ((unit_end - unit_start) * tb_num * 10) / (tb_den * 1));
        };

        const auto direct_path = dir / "audio-direct.mp4";
        chronon3d::media::MuxSession mux;
        const auto declaration = chronon3d::cli::open_source_declared_mux(
            source_audio.string(), direct_path.string(), mux,
            /*include_audio=*/true);
        REQUIRE(declaration.has_value());
        for (std::size_t index = 0; index < units.size(); ++index) {
            const auto& plan = units[index];
            if (plan.copy_packets()) {
                const auto unit = chronon3d::media::GopWorkUnit{
                    .ordinal = index,
                    .first_pts = plan.first_pts,
                    .last_pts = plan.last_pts,
                    .first_dts = plan.first_dts,
                    .last_dts = plan.last_dts,
                    .kind = chronon3d::media::GopWorkKind::CopyPackets,
                    .source_packets = chronon3d::media::TimestampRange{
                        plan.first_dts, plan.last_dts, true},
                    .compatibility = plan.compatibility};
                REQUIRE(chronon3d::cli::submit_gop_source_packets(
                            source_audio.string(), unit, mux, schedule_origin)
                            .has_value());
                continue;
            }
            const int frames = frames_for(index);
            REQUIRE(frames > 0);
            mux.set_video_origin(plan.first_pts - schedule_origin);
            chronon3d::cli::NativeAvEncoder encoder;
            auto options = chronon3d::cli::test_gop_parity::parity_encoder_options(
                (dir / "unused.mp4").string());
            options.borrowed_mux = &mux;
            options.borrowed_declared_stream = *declared_audio;
            REQUIRE(encoder.open(options));
            for (int frame = 0; frame < frames; ++frame) {
                chronon3d::Framebuffer rendered(64, 64);
                REQUIRE(encoder.write_frame(rendered));
            }
            REQUIRE(encoder.close());
        }
        const chronon3d::media::TimestampRange window{
            schedule_origin, request_end, true};
        const auto audio = chronon3d::cli::submit_source_audio(
            source_audio.string(), mux, window, schedule_origin);
        REQUIRE(audio.has_value());
        REQUIRE(mux.finalize());

        // Counted from the SOURCE itself, in the audio time base, over the same
        // half-open window the submission used.
        const AVRational video_tb{static_cast<int>(tb_num),
                                  static_cast<int>(tb_den)};
        const AVRational audio_tb{audio_facts.audio.time_base_num,
                                  audio_facts.audio.time_base_den};
        const std::int64_t audio_first =
            av_rescale_q(schedule_origin, video_tb, audio_tb);
        const std::int64_t audio_last =
            av_rescale_q(request_end, video_tb, audio_tb);
        std::uint64_t expected_audio = 0;
        for (const auto pts : audio_facts.audio.packet_pts) {
            if (pts >= audio_first && pts < audio_last) ++expected_audio;
        }
        CHECK(expected_audio > 0);
        CHECK(audio->audio_packets == expected_audio);

        const auto produced =
            chronon3d::cli::test_gop_parity::probe_container(direct_path.string());
        REQUIRE(produced.valid);
        CHECK(produced.audio.present);
        CHECK(produced.audio.packet_count == expected_audio);
        CHECK(produced.decoded_ok);
        // Video is unchanged by the presence of the track.
        CHECK(produced.video.packets.size() == 60);
    }

    if (std::getenv("CHRONON3D_KEEP_PARITY_FIXTURES") == nullptr) {
        std::filesystem::remove_all(dir, cleanup_error);
    } else {
        MESSAGE("fixtures kept in " << dir.string());
    }
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
