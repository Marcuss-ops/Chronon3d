#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// check_audio.hpp — loudness/peak/clipping findings for `check`.
//
// Integrated loudness is BS.1770-4 on the mono 48 kHz signal produced by the
// same native decoder the render-plan audio baker uses. Peak is sample peak,
// not true peak; the report says so explicitly.
// ─────────────────────────────────────────────────────────────────────────────

#include "check_findings.hpp"

#include <string>
#include <vector>

namespace chronon3d::cli::check {

struct AudioMetrics {
    bool has_audio_stream{false};
    bool measured{false};
    double duration_seconds{0.0};
    double integrated_lufs{-120.0};
    double peak_dbfs{-120.0};
    double clipped_fraction{0.0};
    std::string backend_status;
    std::string error;
};

/// Decode the first audio stream and measure it. Never throws.
[[nodiscard]] AudioMetrics analyze_audio(const std::string& input);

/// Pure mapping from metrics to findings.
[[nodiscard]] std::vector<Finding> analyze_audio_metrics(
    const AudioMetrics& metrics, const Thresholds& thresholds);

/// BS.1770-4 integrated loudness. Returns -infinity for silence or a signal
/// shorter than one gating block. Coefficients are the 48 kHz K-weighting pair.
[[nodiscard]] double integrated_loudness_lufs(const std::vector<float>& samples,
                                              int sample_rate);

/// Maximum absolute sample value in dBFS. Returns -infinity for silence.
[[nodiscard]] double sample_peak_dbfs(const std::vector<float>& samples);

} // namespace chronon3d::cli::check
