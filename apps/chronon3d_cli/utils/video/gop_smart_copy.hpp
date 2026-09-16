#pragma once

#include "bitstream_target_contract.hpp"

#include <algorithm>
#include <cstdint>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <optional>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::media {
// Forward declaration only: this header must stay usable in a video build
// without native FFmpeg, where packet_assembler.hpp's libav includes are not
// available. Every use below is a reference parameter.
class MuxSession;
} // namespace chronon3d::media

namespace chronon3d::cli {

enum class GopExecutionMode : std::uint8_t {
    Copy,
    Reencode,
};

// Per-stream bitstream compatibility gate. Before splicing source H.264
// packets and NVENC-produced H.264 packets into one container the resolver
// MUST verify that the two bitstreams are structurally compatible.
// A mismatch on any of these fields means the decoder will corrupt on
// the splice boundary (green frames, seek errors, player-specific crashes).
//
// The gate is fail-closed: safe_to_splice() returns false unless every
// field has been explicitly verified (no default-true).
//
// `BitstreamTargetContract` / `BitstreamTargetProducer` (the identity of the
// target side) live in bitstream_target_contract.hpp so the encoder layer can
// describe its own output without depending on this module.
using BitstreamCompatibility = chronon3d::media::BitstreamCompatibility;

/// Normalize a codec configuration to the parameter-set NAL payloads it
/// carries, in the order the stream declares them.
///
/// The same stream is described in two shapes depending on who is talking: a
/// demuxer reports an avcC/hvcC CONFIGURATION RECORD (what a playback decoder
/// receives from the container), while an encoder publishes its SPS/PPS
/// IN-BAND as Annex-B NALs (libx264 and NVENC both do). Comparing those two
/// byte-for-byte would reject a stream the encoder reproduced exactly, so the
/// splice gate compares the NAL payloads instead — the real question is
/// whether the same parameter sets are described, not how they are wrapped.
///
/// Video-only parameter sets (H.264 SPS/PPS, HEVC VPS/SPS/PPS) are kept; every
/// other NAL (SEI, slice) is ignored. An unrecognized layout yields an empty
/// list, which callers must treat as "cannot certify".
[[nodiscard]] std::vector<std::vector<std::uint8_t>> parameter_set_nals(
    BitstreamCodecId codec, const std::uint8_t* data, std::size_t size);

/// Leading Annex-B parameter-set prefix of an encoded packet payload.
///
/// Encoders that are NOT opened with a global header (libx264's default, and
/// how this pipeline opens them) repeat SPS/PPS at the start of every keyframe
/// and leave the codec context's extradata empty, so the packet payload is the
/// only place their parameter sets exist. The scan stops at the first NAL that
/// is not a parameter set, which is what keeps it away from slice data.
///
/// Returns the number of leading payload bytes that ARE parameter sets (0 when
/// the payload does not begin with one).
[[nodiscard]] std::size_t leading_parameter_set_prefix_size(
    BitstreamCodecId codec, const std::uint8_t* data, std::size_t size) noexcept;

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
/// Compare the complete decoder configuration needed to splice two encoded
/// video streams. Every field is explicit and defaults to false: callers
/// must provide both source and output parameters, otherwise copying is
/// rejected.
[[nodiscard]] BitstreamCompatibility compare_bitstream_compatibility(
    const AVCodecParameters& source,
    const BitstreamTargetContract& target,
    bool random_access_safe) noexcept;
#endif

struct GopAnalysis {
    std::int64_t first_pts{0};
    std::int64_t last_pts{0};
    bool codec_parameters_match{false};
    BitstreamCompatibility compatibility{};
    bool closed{false};
    bool safe_random_access{false};
    bool intersects_edit{true};
    std::int64_t first_dts{0};
    std::int64_t last_dts{0};
};

struct CompressedPacketInfo {
    std::int64_t pts{0};
    std::int64_t dts{0};
    bool keyframe{false};
    bool references_prior_gop{false};
    /// Synthetic packet descriptions default to certified so the pure
    /// planner remains useful without a codec parser. Real demuxed packets
    /// set this explicitly; unknown syntax stays fail-closed.
    bool random_access_certified{true};
};

/// Tests an edit interval against the presentation-time envelope of a GOP.
/// Packets are supplied in decode order, so using only the first/last packet
/// PTS would miss edits that touch a B-frame in the middle of the group.
[[nodiscard]] inline bool gop_intersects_edit(
    std::span<const CompressedPacketInfo> packets,
    std::int64_t edit_start,
    std::int64_t edit_end) noexcept {
    if (packets.empty() || edit_end <= edit_start) return false;
    std::int64_t min_pts = packets.front().pts;
    std::int64_t max_pts = packets.front().pts;
    for (const auto& packet : packets.subspan(1)) {
        min_pts = std::min(min_pts, packet.pts);
        max_pts = std::max(max_pts, packet.pts);
    }
    return max_pts >= edit_start && min_pts < edit_end;
}

/// Derives packet-level GOP safety from a demuxed packet window. The caller
/// supplies codec-parameter compatibility and whether the edit intersects
/// the window; no decoder is needed for an untouched safe GOP.
[[nodiscard]] inline GopAnalysis analyze_gop(
    std::span<const CompressedPacketInfo> packets,
    bool codec_parameters_match, bool intersects_edit) noexcept {
    if (packets.empty()) {
        return {.codec_parameters_match = false,
                .closed = false,
                .safe_random_access = false,
                .intersects_edit = intersects_edit};
    }
    // Packet iteration is decode order. B-frame streams may legitimately
    // reorder presentation timestamps, so PTS monotonicity is not a GOP
    // safety requirement. DTS must remain monotonic for the packet stream
    // that is handed to the muxer/decoder.
    bool monotonic_dts = true;
    for (std::size_t i = 1; i < packets.size(); ++i) {
        monotonic_dts = monotonic_dts &&
            packets[i - 1].dts <= packets[i].dts;
    }
    const auto& first = packets.front();
    const auto min_pts = std::min_element(
        packets.begin(), packets.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.pts < rhs.pts; });
    const auto max_pts = std::max_element(
        packets.begin(), packets.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.pts < rhs.pts; });
    return {
        // The schedule is expressed in presentation order even though the
        // packets arrive in decode order. This is essential for B-frame GOPs.
        .first_pts = min_pts->pts,
        .last_pts = max_pts->pts,
        .codec_parameters_match = codec_parameters_match,
        .closed = first.keyframe && !first.references_prior_gop &&
            first.random_access_certified,
        .safe_random_access = first.keyframe && !first.references_prior_gop &&
            first.random_access_certified && monotonic_dts,
        .intersects_edit = intersects_edit,
        .first_dts = first.dts,
        .last_dts = packets.back().dts,
    };
}

struct GopPlan {
    std::int64_t first_pts{0};
    std::int64_t last_pts{0};
    std::int64_t first_dts{0};
    std::int64_t last_dts{0};
    /// Decode dependency closure. The output/render range remains the
    /// current GOP, while an Open-GOP may need packets from an earlier unit.
    std::int64_t decode_first_pts{0};
    std::int64_t decode_first_dts{0};
    GopExecutionMode mode{GopExecutionMode::Reencode};
    // Stable ordinal in source presentation order. The hybrid executor uses
    // this to preserve packet ordering when copy and reencode work completes
    // through different paths.
    std::size_t ordinal{0};
    // Compatibility gate for this GOP. When mode == Copy this MUST be
    // safe_to_splice() or the splice will corrupt the decoder. When
    // mode == Reencode the gate is informational (the reencode produces
    // a self-contained GOP that does not depend on source parameters).
    BitstreamCompatibility compatibility{};

    [[nodiscard]] bool copy_packets() const noexcept {
        return mode == GopExecutionMode::Copy && compatibility.safe_to_splice();
    }

    [[nodiscard]] bool reencode_packets() const noexcept {
        return !copy_packets();
    }
};

struct GopSourceAnalysis {
    std::vector<GopPlan> plans;
    std::int64_t first_pts{0};
    std::int64_t last_pts{0};
    // Aggregate: true when ALL GOPs in the plan are Copy AND every
    // compatibility gate is safe. This is the condition for the
    // whole-clip BitstreamCopy fast path (zero NVDEC/CUDA/NVENC).
    bool all_copy_eligible{false};
    // Number of GOPs planned for Copy vs Reencode.
    std::size_t copy_count{0};
    std::size_t reencode_count{0};
    // Time base for all PTS/DTS values in plans. Keeping it with the analysis
    // prevents an executor from interpreting source ticks as output frames.
    std::int32_t source_time_base_num{0};
    std::int32_t source_time_base_den{1};
    // The inspected source window is narrower than the source clip and has
    // no pixel edits. This maps to VideoExecutionPath::TrimPackets.
    bool trim_only{false};
    // When all_copy_eligible is false but copy_count > 0, the plan is
    // a hybrid: some GOPs are copied, others are re-encoded. The
    // caller routes both to the same PacketAssembler.
    [[nodiscard]] bool is_hybrid() const noexcept {
        return copy_count > 0 && reencode_count > 0;
    }

    [[nodiscard]] bool valid() const noexcept {
        return !plans.empty() && copy_count + reencode_count == plans.size();
    }
};

struct GopCopyResult {
    std::uint64_t video_packets{0};
    std::uint64_t audio_packets{0};
};

/// ── ONE mapping: GOP analysis → compiler-owned schedule ────────────────
///
/// The preflight that feeds the resolver and every executor that consumes the
/// schedule go through this function, so a work unit can never mean two
/// different things depending on who built it. `splice_safe` is the caller's
/// certification of the copied units: the preflight derives it from the
/// per-GOP compatibility gate, while the direct-mux lane certifies it by
/// declaring the container FROM the source — its copied packets are the source
/// stream by construction, and a participating encoder must reproduce that
/// stream.
[[nodiscard]] chronon3d::media::GopExecutionPlan build_gop_plan_from_analysis(
    const GopSourceAnalysis& analysis, bool splice_safe);

/// Copy the selected compressed interval directly into the output container.
/// The caller must have established source/composition equivalence; this
/// function deliberately performs no scene rendering or pixel conversion.
[[nodiscard]] std::optional<GopCopyResult> copy_gop_source(
    const std::string& source_path,
    const std::string& output_path,
    double start_seconds,
    double end_seconds);

// ── Source-declared container (direct-mux / splice lane, P0-5D) ───────────
//
// On the splice lane the container is declared from the COMPRESSED SOURCE, not
// from the encoder: a copied packet is then correct by construction, and the
// only thing left to prove is that the encoder reproduces the declared stream.
// That proof is deterministic — two runs of the same encoder configuration
// emit byte-identical parameter sets — so a same-configuration source
// certifies and a foreign source does not.

/// Facts captured while the container was declared from the source. Enough to
/// certify the encoder against the declared stream without keeping the
/// demuxer (or its AVCodecParameters) alive.
struct SourceMuxDeclaration {
    std::vector<std::uint8_t> video_extradata;
    int video_codec_id{-1};
    int video_profile{-1};
    int video_level{-1};
    int video_pixel_format{-1};
    int video_width{0};
    int video_height{0};
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    AVRational video_time_base{0, 1};
    AVColorRange color_range{AVCOL_RANGE_UNSPECIFIED};
    AVColorSpace color_space{AVCOL_SPC_UNSPECIFIED};
    AVColorPrimaries color_primaries{AVCOL_PRI_UNSPECIFIED};
    AVColorTransferCharacteristic color_trc{AVCOL_TRC_UNSPECIFIED};
#endif
    int source_video_stream{-1};
    int source_audio_stream{-1};
    bool audio_declared{false};

    /// Splice certification for this lane: the encoder's materialized contract
    /// must reproduce the stream the container was declared from. Uses the one
    /// compatibility authority — no second comparison table.
    [[nodiscard]] bool encoder_reproduces(
        const BitstreamTargetContract& encoder_contract) const noexcept;
};

/// Sample framing of a container declared from these parameter sets.
///
/// True when the bytes are an avcC / hvcC configuration record, which is what
/// a demuxer reports for a stream whose SAMPLES are length-prefixed. The
/// distinction decides who must convert: libavformat converts an Annex-B
/// producer's packets automatically only while the output stream carries no
/// extradata, so a container declared from a compressed source needs its
/// producer to emit the declared format itself.
[[nodiscard]] bool parameter_sets_are_config_record(BitstreamCodecId codec,
                                                    const std::uint8_t* data,
                                                    std::size_t size) noexcept;

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
/// Rewrite an Annex-B packet IN PLACE into the length-prefixed sample format a
/// configuration-record container stores, preserving every NAL and the
/// packet's timing, flags and side data. Returns false when the packet is
/// already container-native (or cannot be converted), so the caller can treat
/// it as a no-op rather than a failure.
[[nodiscard]] bool convert_annex_b_to_length_prefixed(
    AVPacket& packet, BitstreamCodecId codec) noexcept;
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
/// Declare `mux`'s video stream (and, when `include_audio`, its audio stream)
/// from the source's own codec parameters, then return the facts the splice
/// certification needs. The caller owns `mux` and finalizes it exactly once.
[[nodiscard]] std::optional<SourceMuxDeclaration> open_source_declared_mux(
    const std::string& source_path, const std::string& output_path,
    chronon3d::media::MuxSession& mux, bool include_audio);
#endif

/// Submit the video packets of ONE work unit into the shared mux, straight
/// from the source's own timestamps.
///
/// `unit.source_packets` is a source-time-base range and `unit.first_dts` /
/// `unit.last_dts` the unit's decode envelope: packets are selected by DTS,
/// which is the order the container stores them in. Nothing round-trips
/// through seconds or frames — that second interpretation of the schedule is
/// exactly what this primitive exists to remove.
///
/// `window_origin` is the SOURCE tick the output container's zero corresponds
/// to — the schedule's own left edge for a trimmed export, 0 for a whole-clip
/// one. It is subtracted from the source tick of every copied packet, in the
/// source time base, with integer arithmetic: the one conversion the schedule
/// needs is a translation, never a round-trip through seconds or frames.
[[nodiscard]] std::optional<GopCopyResult> submit_gop_source_packets(
    const std::string& source_path, const chronon3d::media::GopWorkUnit& unit,
    chronon3d::media::MuxSession& mux,
    std::int64_t window_origin = 0);

/// Submit the source audio track for the SCHEDULED window, once.
///
/// `video_window` is expressed in the SOURCE VIDEO time base — the same
/// authority the work units and the analysis speak — and is converted to the
/// audio stream's time base by the container's own rescale. Seconds are never
/// reintroduced, so the schedule keeps exactly one interpretation.
///
/// `window_origin` (source video ticks) is the same origin
/// submit_gop_source_packets() places the copied video at, rescaled once into
/// the audio stream's time base: without it the video track would start at the
/// container's zero while the audio track kept the source's absolute
/// timestamps, i.e. an A/V offset equal to the trim start.
///
/// Audio is not GOP-aligned, so this is called exactly once per export no
/// matter how many work units ran: a per-unit call would duplicate the track.
/// A window with `present == false` means "the whole track".
[[nodiscard]] std::optional<GopCopyResult> submit_source_audio(
    const std::string& source_path, chronon3d::media::MuxSession& mux,
    chronon3d::media::TimestampRange video_window,
    std::int64_t window_origin = 0);

/// Build the target contract for a WHOLE-CLIP packet copy.
///
/// This lane has no encoder: the output container's video stream is the source
/// stream, so the source's own codec parameters (including its SPS/PPS) are by
/// definition the output contract. This is the only place a compressed source
/// may produce a target, and it is legal precisely because no encoder is
/// involved. Never use it when a re-encode participates in the same output —
/// use the encoder-owned contract authority instead.
[[nodiscard]] std::optional<BitstreamTargetContract> make_remux_target_contract(
    const std::string& source_path);

/// Inspect a compressed video source without decoding its frames. The edit
/// interval is expressed in seconds and converted to the source stream time
/// base by the analyzer. Packet copy is
/// only planned for complete untouched GOPs whose codec parameters are known
/// to match the requested output — supplied by the caller as `target`, which
/// must be the ENCODER-owned contract whenever a re-encode participates.
[[nodiscard]] std::optional<GopSourceAnalysis> inspect_gop_source(
    const std::string& path,
    const BitstreamTargetContract& target,
    double edit_start_seconds,
    double edit_end_seconds,
    bool content_changed = true,
    std::span<const chronon3d::media::ChangedInterval> changed_intervals = {});

/// Copy is permitted only when every packet-level safety gate is true.
/// The planner is deliberately pure; codec-specific packet inspection feeds
/// GopAnalysis, while rendering/encoding consumes only GopPlan.
///
/// The compatibility gate is derived from the analysis: all the
/// codec_parameters_match / closed / safe_random_access conditions map
/// directly to BitstreamCompatibility fields. A caller with deeper
/// codec knowledge (SPS/PPS, profile/level) can override the gate before
/// committing the plan.
[[nodiscard]] constexpr GopPlan plan_gop(const GopAnalysis& analysis) noexcept {
    const bool copy = analysis.codec_parameters_match && analysis.closed &&
        analysis.safe_random_access && !analysis.intersects_edit;
    BitstreamCompatibility compat = analysis.compatibility;
    // The aggregate codec flag is retained for diagnostics only. It must
    // never promote an incomplete compatibility record to Copy.
    compat.random_access_safe = compat.random_access_safe &&
        analysis.safe_random_access;
    return GopPlan{
        .first_pts = analysis.first_pts,
        .last_pts = analysis.last_pts,
        .first_dts = analysis.first_dts,
        .last_dts = analysis.last_dts,
        .mode = copy ? GopExecutionMode::Copy : GopExecutionMode::Reencode,
        .ordinal = 0,
        .compatibility = compat,
    };
}

} // namespace chronon3d::cli
