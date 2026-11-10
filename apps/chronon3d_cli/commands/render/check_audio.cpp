#include "check_audio.hpp"

#include <src/render_plan/audio_react_baker.hpp>

#include <chronon3d/media/video/media_probe.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace chronon3d::cli::check {
namespace {

constexpr int kCanonicalSampleRate = 48000;

// BS.1770-4 K-weighting, 48 kHz coefficients.
constexpr double kShelfB0 = 1.53512485958697;
constexpr double kShelfB1 = -2.69169618940638;
constexpr double kShelfB2 = 1.19839281085285;
constexpr double kShelfA1 = -1.69065929318241;
constexpr double kShelfA2 = 0.73248077421585;

constexpr double kHighPassB0 = 1.0;
constexpr double kHighPassB1 = -2.0;
constexpr double kHighPassB2 = 1.0;
constexpr double kHighPassA1 = -1.99004745483398;
constexpr double kHighPassA2 = 0.99007225036621;

[[nodiscard]] double to_lufs(const double mean_square) noexcept {
    return -0.691 + 10.0 * std::log10(mean_square);
}

template <typename Samples>
void apply_biquad(const Samples& input, std::vector<double>& output,
                  const double b0, const double b1, const double b2,
                  const double a1, const double a2) {
    output.resize(input.size());
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
    for (std::size_t index = 0; index < input.size(); ++index) {
        const double x0 = static_cast<double>(input[index]);
        const double y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        output[index] = y0;
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = y0;
    }
}

} // namespace

double integrated_loudness_lufs(const std::vector<float>& samples,
                                const int sample_rate) {
    if (sample_rate <= 0 || samples.empty()) {
        return -std::numeric_limits<double>::infinity();
    }
    const int block = sample_rate * 4 / 10;  // 400 ms
    const int hop = sample_rate / 10;        // 100 ms
    if (block <= 0 || hop <= 0 ||
        samples.size() < static_cast<std::size_t>(block)) {
        return -std::numeric_limits<double>::infinity();
    }

    std::vector<double> shelf;
    std::vector<double> weighted;
    apply_biquad(samples, shelf, kShelfB0, kShelfB1, kShelfB2,
                 kShelfA1, kShelfA2);
    apply_biquad(shelf, weighted, kHighPassB0, kHighPassB1, kHighPassB2,
                 kHighPassA1, kHighPassA2);

    std::vector<double> energies;
    energies.reserve((weighted.size() - static_cast<std::size_t>(block)) /
                         static_cast<std::size_t>(hop) + 1);
    for (std::size_t start = 0;
         start + static_cast<std::size_t>(block) <= weighted.size();
         start += static_cast<std::size_t>(hop)) {
        double sum = 0.0;
        for (int offset = 0; offset < block; ++offset) {
            const double value = weighted[start + static_cast<std::size_t>(offset)];
            sum += value * value;
        }
        energies.push_back(sum / static_cast<double>(block));
    }
    if (energies.empty()) {
        return -std::numeric_limits<double>::infinity();
    }

    double gated_sum = 0.0;
    int gated_count = 0;
    for (const double energy : energies) {
        if (energy <= 0.0) continue;
        if (to_lufs(energy) > -70.0) {
            gated_sum += energy;
            ++gated_count;
        }
    }
    if (gated_count == 0) {
        return -std::numeric_limits<double>::infinity();
    }
    const double ungated = gated_sum / static_cast<double>(gated_count);
    const double relative_gate = to_lufs(ungated) - 10.0;

    double relative_sum = 0.0;
    int relative_count = 0;
    for (const double energy : energies) {
        if (energy <= 0.0) continue;
        const double lufs = to_lufs(energy);
        if (lufs > -70.0 && lufs > relative_gate) {
            relative_sum += energy;
            ++relative_count;
        }
    }
    if (relative_count == 0) {
        return -std::numeric_limits<double>::infinity();
    }
    return to_lufs(relative_sum / static_cast<double>(relative_count));
}

double sample_peak_dbfs(const std::vector<float>& samples) {
    double peak = 0.0;
    for (const float sample : samples) {
        peak = std::max(peak, std::abs(static_cast<double>(sample)));
    }
    if (peak <= 0.0) {
        return -std::numeric_limits<double>::infinity();
    }
    return 20.0 * std::log10(peak);
}

AudioMetrics analyze_audio(const std::string& input) {
    AudioMetrics metrics;
#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    metrics.backend_status = "native_ffmpeg";
    const auto probe = media::video::probe_media(input);
    if (!probe) {
        metrics.error = "could not probe audio: " + probe.error().message;
        return metrics;
    }
    const auto audio_it = std::find_if(
        probe->streams.begin(), probe->streams.end(),
        [](const auto& stream) {
            return stream.kind == media::video::MediaStreamKind::Audio;
        });
    if (audio_it == probe->streams.end()) {
        metrics.error = "no audio stream";
        return metrics;
    }
    metrics.has_audio_stream = true;

    std::vector<float> samples;
    std::string decode_error;
    if (!render_plan::detail::decode_audio_mono_48k(
            input, samples, decode_error)) {
        metrics.error = decode_error.empty()
            ? "audio decode failed" : decode_error;
        return metrics;
    }
    if (samples.empty()) {
        metrics.error = "audio stream decoded to no samples";
        return metrics;
    }

    metrics.measured = true;
    metrics.duration_seconds =
        static_cast<double>(samples.size()) / kCanonicalSampleRate;
    const double loudness = integrated_loudness_lufs(samples, kCanonicalSampleRate);
    const double peak = sample_peak_dbfs(samples);
    metrics.integrated_lufs = std::isfinite(loudness) ? loudness : -120.0;
    metrics.peak_dbfs = std::isfinite(peak) ? peak : -120.0;

    std::uint64_t clipped = 0;
    for (const float sample : samples) {
        if (std::abs(static_cast<double>(sample)) >= 0.999) ++clipped;
    }
    metrics.clipped_fraction =
        static_cast<double>(clipped) / static_cast<double>(samples.size());
    return metrics;
#else
    metrics.backend_status = "native_ffmpeg_unavailable";
    metrics.error = "audio check requires CHRONON3D_ENABLE_NATIVE_FFMPEG";
    return metrics;
#endif
}

std::vector<Finding> analyze_audio_metrics(const AudioMetrics& metrics,
                                           const Thresholds& thresholds) {
    std::vector<Finding> findings;
    if (!metrics.has_audio_stream) return findings;

    if (!metrics.measured) {
        Finding finding;
        finding.id = "audio.not_measured";
        finding.category = "audio";
        finding.severity = Severity::P2;
        finding.message = "an audio stream exists but it could not be measured";
        finding.fix = "inspect the decode error; do not treat the audio as verified";
        finding.details = {
            {"backend", metrics.backend_status},
            {"error", metrics.error},
        };
        findings.push_back(std::move(finding));
        return findings;
    }

    if (metrics.integrated_lufs <= thresholds.silence_lufs) {
        Finding finding;
        finding.id = "audio.silent";
        finding.category = "audio";
        finding.severity = Severity::P1;
        finding.message = "the audio track is silent or below the floor";
        finding.fix = "check the audio mix, the source asset and the mux";
        finding.details = {
            {"integrated_lufs", metrics.integrated_lufs},
            {"threshold_lufs", thresholds.silence_lufs},
        };
        findings.push_back(std::move(finding));
    } else {
        const double delta =
            std::abs(metrics.integrated_lufs - thresholds.loudness_target_lufs);
        if (delta > thresholds.loudness_tolerance_lu) {
            Finding finding;
            finding.id = "audio.loudness";
            finding.category = "audio";
            finding.severity = Severity::P1;
            finding.message = "integrated loudness is outside the delivery target";
            finding.fix = "adjust the mix gain or the target; BS.1770 integrated "
                          "loudness is measured, not peak-normalized";
            finding.details = {
                {"integrated_lufs", metrics.integrated_lufs},
                {"target_lufs", thresholds.loudness_target_lufs},
                {"tolerance_lu", thresholds.loudness_tolerance_lu},
                {"delta_lu", delta},
            };
            findings.push_back(std::move(finding));
        }
    }

    if (metrics.peak_dbfs > thresholds.max_peak_dbfs) {
        Finding finding;
        finding.id = "audio.peak";
        finding.category = "audio";
        finding.severity = Severity::P1;
        finding.message = "sample peak is above the configured ceiling";
        finding.fix = "lower the master gain; this is sample peak, not true peak";
        finding.details = {
            {"sample_peak_dbfs", metrics.peak_dbfs},
            {"threshold_dbfs", thresholds.max_peak_dbfs},
        };
        findings.push_back(std::move(finding));
    }

    if (metrics.clipped_fraction > thresholds.max_audio_clipped_fraction) {
        Finding finding;
        finding.id = "audio.clipping";
        finding.category = "audio";
        finding.severity = Severity::P2;
        finding.message = "audio samples are clipping at full scale";
        finding.fix = "lower the gain before the limiter or fix the mix";
        finding.details = {
            {"clipped_fraction", metrics.clipped_fraction},
            {"threshold_fraction", thresholds.max_audio_clipped_fraction},
        };
        findings.push_back(std::move(finding));
    }

    std::sort(findings.begin(), findings.end(),
        [](const Finding& a, const Finding& b) {
            return static_cast<int>(a.severity) < static_cast<int>(b.severity);
        });
    return findings;
}

} // namespace chronon3d::cli::check
