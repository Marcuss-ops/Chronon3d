#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// frame_timing_summary.hpp — the SINGLE per-frame timing summary.
//
// Chronon has ONE per-frame timing summary.  It is computed ONLY at
// finalize time from the TelemetrySession's frame records (never
// incrementally during the render loop), and it is the shared
// implementation used by both the video-export sidecar and the preset
// certification harness.  Do NOT re-derive first/mean/p95/p99 inline: those
// percentiles belong here, not in a second collector or a per-caller helper.
//
// This is a pure function over the existing `FrameTelemetry` records — it
// introduces no new collector, no registry and no store.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>  // FrameTelemetry

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chronon3d::telemetry {

/// Canonical summary of per-frame WALL durations (FrameTelemetry::duration_ms).
///
///   first / min / max / mean / stddev — absolute wall-duration statistics.
///   p50 / p90 / p95 / p99             — wall-duration percentiles.
///   warmup_*                          — statistics over the warmup window
///                                       (first N frames, where caches/allocators
///                                       are still cold).
///   steady_*                          — statistics over the steady-state slice
///                                       (everything after the warmup window).
///
/// Very short renders (< 10 frames) disable the warmup window entirely
/// (warmup_frames == 0), so steady stats then cover every frame.
struct FrameTimingSummary {
    double first_frame_ms{0.0};
    double min_frame_ms{0.0};
    double max_frame_ms{0.0};
    double mean_frame_ms{0.0};
    double stddev_frame_ms{0.0};

    double p50_frame_ms{0.0};
    double p90_frame_ms{0.0};
    double p95_frame_ms{0.0};
    double p99_frame_ms{0.0};

    std::size_t warmup_frames{0};
    double warmup_avg_ms{0.0};
    double steady_avg_ms{0.0};
    double steady_p50_ms{0.0};
    double steady_p95_ms{0.0};
    double steady_p99_ms{0.0};
};

/// Compute the canonical per-frame timing summary.  The warmup window is
/// derived from the ORIGINAL frame order (the first frames of a job warm
/// caches), so it is captured before the durations are sorted for percentile
/// indexing.  Deterministic: identical input frames → identical summary.
[[nodiscard]] inline FrameTimingSummary summarize_frame_timings(
    const std::vector<FrameTelemetry>& frames) {
    FrameTimingSummary summary;
    if (frames.empty()) return summary;

    std::vector<double> durations;
    durations.reserve(frames.size());
    for (const auto& frame : frames) {
        durations.push_back(frame.duration_ms);
    }

    summary.first_frame_ms = durations.front();
    const std::size_t count = durations.size();

    // Warmup window over the original (pre-sort) order.
    const std::size_t warmup_frames = count >= 10 ? 5 : 0;
    summary.warmup_frames = warmup_frames;
    if (warmup_frames > 0 && count >= warmup_frames) {
        double warmup_sum = 0.0;
        for (std::size_t i = 0; i < warmup_frames; ++i) {
            warmup_sum += durations[i];
        }
        summary.warmup_avg_ms = warmup_sum / static_cast<double>(warmup_frames);
    }

    std::sort(durations.begin(), durations.end());

    const auto percentile = [&durations](double p) {
        if (durations.empty()) return 0.0;
        const auto index =
            static_cast<std::size_t>(p * static_cast<double>(durations.size() - 1));
        return durations[index];
    };
    summary.p50_frame_ms = percentile(0.50);
    summary.p90_frame_ms = percentile(0.90);
    summary.p95_frame_ms = percentile(0.95);
    summary.p99_frame_ms = percentile(0.99);

    // Steady-state slice: everything after the warmup window (sorted).
    const std::size_t steady_count = count - warmup_frames;
    if (steady_count > 0) {
        double steady_sum = 0.0;
        for (std::size_t i = warmup_frames; i < count; ++i) {
            steady_sum += durations[i];
        }
        summary.steady_avg_ms = steady_sum / static_cast<double>(steady_count);
    }
    const auto steady_percentile =
        [&durations, warmup_frames, steady_count](double p) {
            if (steady_count == 0) return 0.0;
            const auto index = warmup_frames + static_cast<std::size_t>(
                p * static_cast<double>(steady_count - 1));
            return durations[index];
        };
    summary.steady_p50_ms = steady_percentile(0.50);
    summary.steady_p95_ms = steady_percentile(0.95);
    summary.steady_p99_ms = steady_percentile(0.99);

    double sum = 0.0;
    for (double value : durations) sum += value;
    summary.mean_frame_ms = count > 0 ? sum / static_cast<double>(count) : 0.0;

    double squared_sum = 0.0;
    for (double value : durations) {
        const double delta = value - summary.mean_frame_ms;
        squared_sum += delta * delta;
    }
    summary.stddev_frame_ms =
        count > 0 ? std::sqrt(squared_sum / static_cast<double>(count)) : 0.0;

    summary.min_frame_ms = durations.front();
    summary.max_frame_ms = durations.back();

    return summary;
}

}  // namespace chronon3d::telemetry
