#pragma once
// ==============================================================================
// chronon3d/include/chronon3d/animation/temporal/temporal_samples.hpp
//
// TemporalSampleSet + generate_temporal_samples()
// Single source of truth for shutter-window sample times, jitter, Halton,
// and normalized reconstruction-filter weights.
//
// PR1 — replaces the duplicated helpers that previously co-existed in:
//   * src/scene/camera/camera_v1/camera_motion_blur.cpp  // drift-class: historical (implementation merged into temporal_samples.cpp)
//       (Halton sequence + sub_sample_times + filter_weight)
//   * src/render_graph/pipeline/composition.cpp
//       (static motion_blur_jitter + motion_blur_filter_weight + Halton inline)
//
// Consumers:
//   * src/scene/camera/camera_v1/internal/shutter_pose_sampler.cpp
//   * src/render_graph/pipeline/composition.cpp (render_composition_frame)
//
// Independence: this module does NOT include MotionBlurSettings or any
// large camera-system header. It only depends on:
//   * chronon3d/core/types/frame.hpp     (Frame)
//   * chronon3d/core/types/types.hpp     (u64)
//   * chronon3d/scene/model/camera/camera_common_types.hpp
//       (TemporalSamplePattern, TemporalFilter — already lightweight)
//
// Determinism: identical (params, num_samples, center_frame) produce
// identical output. Two calls in two processes → byte-equal TemporalSampleSet.
// ==============================================================================
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/scene/model/camera/camera_common_types.hpp>  // TemporalSamplePattern, TemporalFilter

#include <algorithm>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace chronon3d::temporal {

// ── Settings snapshot used at sample-time ─────────────────────────────────────
// Sparse struct so callers can use this module without depending on the
// full MotionBlurSettings struct (e.g. ShutterPoseSampler which is internal).
//
// Shutter math (matches the conventions previously inlined in
// camera_motion_blur.cpp and composition.cpp):
//
//   exposure_normalized   = shutter_angle_deg / 360.0          ∈ (0, 1+]
//   window_start_norm     = shutter_phase_deg / 360.0
//   sub_frame_in_window   = (s + 0.5 + jitter) / num_samples    ∈ [0, 1]
//   absolute_sub_frame    = center_frame + window_start_norm
//                           + sub_frame_in_window * exposure_norm
struct TemporalSampleParams {
    double shutter_angle_deg{180.0};           // 180° = half-frame exposure
    double shutter_phase_deg{-90.0};           // -90° = centered on frame

    TemporalSamplePattern pattern{TemporalSamplePattern::Stratified};
    TemporalFilter        filter{TemporalFilter::Box};
    std::uint64_t         jitter_seed{0x3A5C9F1E};  // arbitrary non-zero
};

// ── Output bundle ─────────────────────────────────────────────────────────────
//
// `sample_times[i]`         ∈ [0, 1]: fraction within the shutter window
// `normalized_weights[i]`  ∈ [0, 1], SUM(normalized_weights) == 1.0
//                           ready to drop into a premultiplied accumulator
//                           (`dst += src * w`). Already accounts for both
//                           filter shape and uniform-vs-Halton spacing so a
//                           Box+Uniform set yields `w_i == 1/N` for all i.
//
// Window geometry is reported in *normalized* units (frame-fraction) so the
// two callers can apply their unit conversion in one place without re-deriving
// the shutter-window math.
struct TemporalSampleSet {
    std::vector<double> sample_times;             // ∈ [0, 1]
    std::vector<float>  normalized_weights;       // sum to 1.0
    double exposure_normalized{0.0};             // = shutter_angle_deg / 360
    double window_start_normalized{0.0};         // = shutter_phase_deg / 360

    [[nodiscard]] int num_samples() const noexcept {
        return static_cast<int>(sample_times.size());
    }

    [[nodiscard]] bool empty() const noexcept {
        return sample_times.empty();
    }
};

// ── PRIMARY ENTRY POINT ──────────────────────────────────────────────────────
//
// Generates a deterministic TemporalSampleSet of `num_samples` weighted
// sub-frames within the configured shutter window.
//
// Args:
//   params        — shutter angle/phase/pattern/filter/seed
//   num_samples   — N >= 1. If N == 0, returns an empty set with geometry
//                   metadata only (useful for early bail-outs in callers).
//   center_frame  — used only as a seed component (deterministic per-frame
//                   jitter). Does not appear in the output geometry.
//
// Returns: TemporalSampleSet with weights already normalized to sum 1.
//
// Determinism contract: bytes-equal for bytes-equal inputs.
TemporalSampleSet generate_temporal_samples(
    const TemporalSampleParams& params,
    int num_samples,
    Frame center_frame);

// ── Isolated render sample contract ─────────────────────────────────────────
//
// A SampleContext is the complete identity of one temporal render.  The
// renderer uses `time` for evaluation and `cache_key` for every frame/node
// cache lookup.  Keeping both values together prevents a sample from being
// evaluated at one instant while being cached as another.
struct SampleContext {
    std::size_t index{0};
    double normalized_time{0.0};
    SampleTime time{};
    TemporalSampleKey cache_key{};
    float weight{0.0f};
};

/// Explicit shutter-window plan consumed by temporal accumulation.
///
/// The plan is immutable after construction and has a bounded sample count so
/// a malformed render request cannot allocate an unbounded number of scratch
/// contexts.  Each context has a distinct sub-frame cache identity; the
/// caller owns the actual cache/scratch resources and must keep them isolated
/// for the duration of that context.
struct TemporalBudgetResolver {
    /// Hard ceiling in aggregate sample pixels (not bytes).
    static constexpr std::uint64_t kHardSafetyCeiling = 128ULL * 1024ULL * 1024ULL;

    /// Zero selects the canonical hard ceiling for legacy/internal callers;
    /// non-zero values can only lower the hard safety ceiling.
    std::uint64_t max_temporal_pixels{0};

    [[nodiscard]] std::uint64_t effective_max_temporal_pixels() const noexcept {
        return max_temporal_pixels == 0
            ? kHardSafetyCeiling
            : std::min(max_temporal_pixels, kHardSafetyCeiling);
    }

    [[nodiscard]] bool allows(
        int samples,
        std::size_t width,
        std::size_t height) const noexcept {
        if (samples <= 0 || width == 0 || height == 0) return false;
        constexpr auto max_u64 = std::numeric_limits<std::uint64_t>::max();
        if (static_cast<std::uint64_t>(width) > max_u64 /
            static_cast<std::uint64_t>(height)) {
            return false;
        }
        const auto pixels = static_cast<std::uint64_t>(width) *
            static_cast<std::uint64_t>(height);
        return pixels <= effective_max_temporal_pixels() /
            static_cast<std::uint64_t>(samples);
    }
};

struct TemporalSamplePlan {
    static constexpr int kMaxSamples = 64;

    std::vector<SampleContext> contexts;
    int requested_samples{0};
    bool rejected{false};
    double exposure_normalized{0.0};
    double window_start_normalized{0.0};

    [[nodiscard]] bool valid() const noexcept {
        if (rejected || requested_samples <= 0 ||
            requested_samples > kMaxSamples ||
            contexts.size() != static_cast<std::size_t>(requested_samples)) {
            return false;
        }
        float weight_sum = 0.0f;
        for (std::size_t i = 0; i < contexts.size(); ++i) {
            const auto& context = contexts[i];
            if (!std::isfinite(context.time.frame) ||
                !std::isfinite(context.normalized_time) ||
                context.normalized_time < 0.0 ||
                context.normalized_time > 1.0 ||
                !std::isfinite(context.weight) || context.weight <= 0.0f) {
                return false;
            }
            for (std::size_t j = i + 1; j < contexts.size(); ++j) {
                if (context.cache_key == contexts[j].cache_key ||
                    context.time == contexts[j].time) {
                    return false;
                }
            }
            weight_sum += context.weight;
        }
        const bool normalized_weights =
            std::abs(weight_sum - 1.0f) <= 1e-4f;
        return std::isfinite(exposure_normalized) &&
            std::isfinite(window_start_normalized) &&
            normalized_weights;
    }

    [[nodiscard]] int num_samples() const noexcept {
        return static_cast<int>(contexts.size());
    }

    [[nodiscard]] const SampleContext& operator[](std::size_t index) const {
        return contexts.at(index);
    }
};

/// Materialize one bounded, deterministic plan from the canonical sample set.
/// The resolver is the sole owner of the aggregate temporal-pixel policy;
/// `cache_version` lets a caller invalidate all sample identities when the
/// authored content version changes without changing the shutter geometry.
[[nodiscard]] TemporalSamplePlan make_temporal_sample_plan(
    const TemporalSampleParams& params,
    int num_samples,
    Frame center_frame,
    FrameRate frame_rate,
    std::size_t width,
    std::size_t height,
    EvaluationVersion cache_version = 0,
    TemporalBudgetResolver budget = {});

} // namespace chronon3d::temporal
