#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// check_video.hpp — frame metrics and temporal findings for `check`.
//
// The analysis is split in two: `measure_frame` reads one decoded framebuffer,
// and `analyze_video` turns the metric series into findings. Only the second
// half is threshold-sensitive, so tests can exercise it with synthetic frames.
// ─────────────────────────────────────────────────────────────────────────────

#include "check_findings.hpp"

#include <optional>
#include <string>
#include <vector>

namespace chronon3d {
class Framebuffer;
}

namespace chronon3d::cli::check {

struct FrameMetrics {
    int frame{0};
    /// Straight-alpha mean linear BT.709 luma over visible pixels.
    double mean_luma{0.0};
    /// Pixels with alpha > 0, as a fraction of the whole frame.
    double visible_fraction{0.0};
    /// Pixels with alpha >= 0.999, as a fraction of the whole frame.
    double opaque_fraction{0.0};
    /// Opaque pixels at or below the linear black floor, over opaque pixels.
    double near_black_fraction{0.0};
    double diff_from_previous{0.0};
    double clipped_high_fraction{0.0};
    double clipped_low_fraction{0.0};
    bool black{false};
};

struct VideoScan {
    std::string input;
    bool is_video{false};
    bool is_image{false};
    bool quick{false};
    double fps{0.0};
    int sample_step{1};
    int total_frames{0};
    int sampled_frames{0};
    std::vector<FrameMetrics> frames;
    std::string decoder_status;
};

struct VideoScanResult {
    std::optional<VideoScan> scan;
    std::string error;
};

/// Measure one frame. `previous` may be null; when present it must be the
/// previously scanned frame (same dimensions, otherwise the diff is 1.0).
[[nodiscard]] FrameMetrics measure_frame(const Framebuffer& framebuffer,
                                         int frame,
                                         const Framebuffer* previous);

/// Turn a metric series into findings. Pure: no I/O, no decoder.
[[nodiscard]] std::vector<Finding> analyze_video(const VideoScan& scan,
                                                 const Thresholds& thresholds);

/// Decode a video/gradient image and measure it. Video uses the native
/// software decoder; images use the canonical image loader.
[[nodiscard]] VideoScanResult scan_video(const std::string& input,
                                         const Thresholds& thresholds,
                                         bool quick);

} // namespace chronon3d::cli::check
