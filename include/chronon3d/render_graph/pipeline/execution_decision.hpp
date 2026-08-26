#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <chronon3d/core/types/frame.hpp>

namespace chronon3d::graph {

/// Canonical execution paths selected before backend execution.
/// ExecutionResolver owns this choice; backends only execute the selected path.
enum class FrameExecutionPath : std::uint8_t {
    CopyGop,
    ReuseSurface,
    SparseTiles,
    SparseYuv,
    FullYuv,
    FullRgb,
};

[[nodiscard]] constexpr std::string_view to_string(FrameExecutionPath path) noexcept {
    switch (path) {
        case FrameExecutionPath::CopyGop: return "CopyGop";
        case FrameExecutionPath::ReuseSurface: return "ReuseSurface";
        case FrameExecutionPath::SparseTiles: return "SparseTiles";
        case FrameExecutionPath::SparseYuv: return "SparseYuv";
        case FrameExecutionPath::FullYuv: return "FullYuv";
        case FrameExecutionPath::FullRgb: return "FullRgb";
    }
    return "Unknown";
}

/// Backend-neutral descriptor for one untouched compressed GOP. It contains
/// only packet/timing metadata; codec inspection remains in the video adapter.
struct CopyGopPlan {
    std::int64_t first_pts{0};
    std::int64_t last_pts{0};
    std::int64_t end_pts_exclusive{0};
    std::int64_t timestamp_offset{0};
    std::uint32_t video_packets{0};
    std::uint32_t audio_packets{0};
};

/// Evidence supplied by the canonical GOP inspector. CopyGop is selected only
/// when every gate is true; an absent/failed gate must fall back to rendering.
struct CopyGopEligibility {
    bool segment_unchanged{false};
    bool starts_on_keyframe{false};
    bool ends_on_gop_boundary{false};
    bool timestamps_compatible{false};
    bool codec_compatible{false};
    bool encoder_settings_compatible{false};
    bool container_compatible{false};
    CopyGopPlan plan{};
    std::string_view failure_reason{};

    [[nodiscard]] bool eligible() const noexcept {
        return segment_unchanged && starts_on_keyframe &&
            ends_on_gop_boundary && timestamps_compatible &&
            codec_compatible && encoder_settings_compatible &&
            container_compatible;
    }
};

/// Backend-neutral initial decision shared by the compiled graph and command
/// plan. The resolver remains the sole producer; this type carries no policy.
struct ExecutionDecision {
    FrameExecutionPath path;
    std::string_view reason;
    std::optional<CopyGopPlan> copy_gop_plan{};

    [[nodiscard]] bool reuses_surface() const noexcept;
    [[nodiscard]] bool renders_full_rgb() const noexcept;
    [[nodiscard]] bool copies_gop() const noexcept;
};

} // namespace chronon3d::graph
