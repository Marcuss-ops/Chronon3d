#pragma once

#include <cstdint>
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

namespace chronon3d::cli {

enum class GopExecutionMode : std::uint8_t {
    Copy,
    Reencode,
};

/// Per-stream bitstream compatibility gate. Before splicing source H.264
/// packets and NVENC-produced H.264 packets into one container the resolver
/// MUST verify that the two bitstreams are structurally compatible.
/// A mismatch on any of these fields means the decoder will corrupt on
/// the splice boundary (green frames, seek errors, player-specific crashes).
///
/// The gate is fail-closed: safe_to_splice() returns false unless every
/// field has been explicitly verified (no default-true).
struct BitstreamCompatibility {
    bool codec_match{false};
    bool profile_match{false};
    bool level_compatible{false};
    bool dimensions_match{false};
    bool pixel_format_match{false};
    bool parameter_sets_compatible{false};  ///< SPS/PPS
    bool color_params_match{false};         ///< color_range/space/transfer/primaries
    bool random_access_safe{false};        ///< closed GOP, no cross-references

    [[nodiscard]] bool safe_to_splice() const noexcept {
        return codec_match && profile_match && level_compatible &&
            dimensions_match && pixel_format_match &&
            parameter_sets_compatible && color_params_match &&
            random_access_safe;
    }
};

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
/// Compare the complete decoder configuration needed to splice two encoded
/// video streams. Every field is explicit and defaults to false: callers
/// must provide both source and output parameters, otherwise copying is
/// rejected.
[[nodiscard]] BitstreamCompatibility compare_bitstream_compatibility(
    const AVCodecParameters& source,
    const AVCodecParameters& output,
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
};

struct CompressedPacketInfo {
    std::int64_t pts{0};
    std::int64_t dts{0};
    bool keyframe{false};
    bool references_prior_gop{false};
};

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
    bool monotonic = true;
    for (std::size_t i = 1; i < packets.size(); ++i) {
        monotonic = monotonic && packets[i - 1].dts <= packets[i].dts &&
            packets[i - 1].pts <= packets[i].pts;
    }
    const auto& first = packets.front();
    const auto& last = packets.back();
    return {
        .first_pts = first.pts,
        .last_pts = last.pts,
        .codec_parameters_match = codec_parameters_match,
        .closed = first.keyframe && !first.references_prior_gop,
        .safe_random_access = first.keyframe && !first.references_prior_gop && monotonic,
        .intersects_edit = intersects_edit,
    };
}

struct GopPlan {
    std::int64_t first_pts{0};
    std::int64_t last_pts{0};
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

/// Copy the selected compressed interval directly into the output container.
/// The caller must have established source/composition equivalence; this
/// function deliberately performs no scene rendering or pixel conversion.
[[nodiscard]] std::optional<GopCopyResult> copy_gop_source(
    const std::string& source_path,
    const std::string& output_path,
    double start_seconds,
    double end_seconds);

/// Inspect a compressed video source without decoding its frames. The edit
/// interval is expressed in seconds and converted to the source stream time
/// base by the analyzer. Packet copy is
/// only planned for complete untouched GOPs whose codec parameters are known
/// to match the requested output.
[[nodiscard]] std::optional<GopSourceAnalysis> inspect_gop_source(
    const std::string& path,
    std::string_view requested_codec,
    double edit_start_seconds,
    double edit_end_seconds);

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
        .mode = copy ? GopExecutionMode::Copy : GopExecutionMode::Reencode,
        .ordinal = 0,
        .compatibility = compat,
    };
}

} // namespace chronon3d::cli
