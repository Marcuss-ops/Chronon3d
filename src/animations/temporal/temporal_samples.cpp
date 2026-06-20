// ==============================================================================
// chronon3d/src/animations/temporal/temporal_samples.cpp
//
// Implementation of chronon3d::temporal::generate_temporal_samples.
//
// PR1 — replaces the duplicated helpers from:
//   * src/scene/camera/camera_v1/camera_motion_blur.cpp (Halton + filter)
//   * src/render_graph/pipeline/composition.cpp (jitter + filter)
//
// The two previous implementations were almost-identical but not byte-equal
// (one used a single-threaded LCG mutated via lambda-capture; the other used
// a stateless hash-keyed function).  This module uses the stateless hash-keyed
// formulation everywhere — it is thread-safe by construction and produces
// identical output regardless of call ordering or thread count.
// ==============================================================================
#include <chronon3d/animation/temporal/temporal_samples.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::temporal {

namespace {

// ──────────────────────────────────────────────────────────────────────────────
// Halton sequence (low-discrepancy, base 2)
//
// Returns value ∈ [0, 1) for index `n` (n >= 1) at base 2.
// Identical formula to the previous CameraMotionBlurIntegrator::halton().
// ──────────────────────────────────────────────────────────────────────────────
[[nodiscard]] double halton_base2(unsigned n) noexcept {
    double result = 0.0;
    double factor = 0.5;
    while (n > 0) {
        result += static_cast<double>(n & 1u) * factor;
        n >>= 1;
        factor *= 0.5;
    }
    return result;
}

// ──────────────────────────────────────────────────────────────────────────────
// Stratified jitter — deterministic hash-based, stateless
//
// Identical to the function previously inlined at the top of
// src/render_graph/pipeline/composition.cpp (the "Stratified" branch).
// Returns a value ∈ [-0.5, 0.5).
// ──────────────────────────────────────────────────────────────────────────────
[[nodiscard]] double stratified_jitter(
    std::uint64_t seed,
    Frame frame,
    int sample_idx,
    int total_samples) noexcept
{
    std::uint64_t h = seed;
    h ^= static_cast<std::uint64_t>(frame.value)        * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<std::uint64_t>(sample_idx)         * 0xBF58476D1CE4E5B9ULL;
    h ^= static_cast<std::uint64_t>(total_samples)      * 0x94D049BB133111EBULL;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    h = h ^ (h >> 31);

    const double u = static_cast<double>(h & 0x7FFFFFFFFFFFFFFFULL)
                   / static_cast<double>(0x8000000000000000ULL);
    return u - 0.5;  // centre in [-0.5, 0.5)
}

// ──────────────────────────────────────────────────────────────────────────────
// Reconstruction filter weight at normalized time t ∈ [0, 1]
//
// Identical to the previous static motion_blur_filter_weight() in
// src/render_graph/pipeline/composition.cpp.  Return value is un-normalized
// (caller normalizes the per-sample weights so they sum to 1.0).
// ──────────────────────────────────────────────────────────────────────────────
[[nodiscard]] double filter_weight(TemporalFilter filter, double t) noexcept {
    switch (filter) {
        case TemporalFilter::Box:
            return 1.0;
        case TemporalFilter::Triangle:
            return 1.0 - std::abs(2.0 * t - 1.0);
        case TemporalFilter::Gaussian: {
            constexpr double kSigma = 0.25;  // exposure_duration / 4
            const double z = (t - 0.5) / kSigma;
            return std::exp(-0.5 * z * z);
        }
    }
    return 1.0;
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
// PR1 — generate_temporal_samples (single canonical implementation)
// ──────────────────────────────────────────────────────────────────────────────
TemporalSampleSet generate_temporal_samples(
    const TemporalSampleParams& params,
    int num_samples,
    Frame center_frame)
{
    TemporalSampleSet out;

    // ── Window geometry (frame-fraction units, in [0, 1+]) ─────────────────
    out.exposure_normalized    = params.shutter_angle_deg / 360.0;
    out.window_start_normalized = params.shutter_phase_deg / 360.0;

    if (num_samples <= 0) {
        // Edge case: caller asked for 0 samples.  Geometry is reported so
        // downstream code can still compute a base sub-frame offset for
        // fast-path rendering.
        return out;
    }

    out.sample_times.reserve(static_cast<std::size_t>(num_samples));
    out.normalized_weights.reserve(static_cast<std::size_t>(num_samples));

    // ── First pass: sample times + un-normalized weights ─────────────────
    double weight_sum = 0.0;
    for (int s = 0; s < num_samples; ++s) {
        // 1) Per-sample jitter offset in [-0.5, 0.5).
        double jitter = 0.0;
        switch (params.pattern) {
            case TemporalSamplePattern::Uniform:
                jitter = 0.0;
                break;
            case TemporalSamplePattern::Stratified:
                jitter = stratified_jitter(params.jitter_seed, center_frame, s, num_samples);
                break;
            case TemporalSamplePattern::Halton:
                jitter = halton_base2(static_cast<unsigned>(s + 1)) - 0.5;
                break;
        }

        // 2) Centred sampling: (s + 0.5 + jitter) / N so samples are
        // distributed *within* their strata, not on the edges. Without
        // +0.5 the first sample lands at t=0 and the distribution bias
        // is -1/(2N) instead of zero.
        const double u = std::clamp(
            (static_cast<double>(s) + 0.5 + jitter) / static_cast<double>(num_samples),
            0.0, 1.0);

        // 3) Reconstruction-filter weight at this t.
        const double raw_w = std::max(0.0, filter_weight(params.filter, u));
        const float  w     = static_cast<float>(raw_w);

        out.sample_times.push_back(u);
        out.normalized_weights.push_back(w);
        weight_sum += raw_w;
    }

    // ── Normalize so weights sum to 1.0 (correct for premultiplied RGBA
    //    weighted accumulation: src * w summed across N samples
    //    converges to src when w_i = 1/N). ─────────────────────────────────
    const float inv_sum = (weight_sum > 0.0)
        ? static_cast<float>(1.0 / weight_sum)
        : 1.0f;
    for (auto& w : out.normalized_weights) {
        w *= inv_sum;
    }

    return out;
}

} // namespace chronon3d::temporal
