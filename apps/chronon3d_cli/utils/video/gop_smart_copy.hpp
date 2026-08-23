#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::cli {

enum class GopExecutionMode : std::uint8_t {
    Copy,
    Reencode,
};

struct GopAnalysis {
    std::int64_t first_pts{0};
    std::int64_t last_pts{0};
    bool codec_parameters_match{false};
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

    [[nodiscard]] bool copy_packets() const noexcept {
        return mode == GopExecutionMode::Copy;
    }
};

struct GopSourceAnalysis {
    std::vector<GopPlan> plans;
    std::int64_t first_pts{0};
    std::int64_t last_pts{0};
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
[[nodiscard]] constexpr GopPlan plan_gop(const GopAnalysis& analysis) noexcept {
    const bool copy = analysis.codec_parameters_match && analysis.closed &&
        analysis.safe_random_access && !analysis.intersects_edit;
    return GopPlan{
        .first_pts = analysis.first_pts,
        .last_pts = analysis.last_pts,
        .mode = copy ? GopExecutionMode::Copy : GopExecutionMode::Reencode,
    };
}

} // namespace chronon3d::cli
