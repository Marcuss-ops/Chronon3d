#pragma once
// ==============================================================================
// chronon3d/include/chronon3d/animation/temporal/temporal_samples.hpp
//
// TemporalSampleSet + generate_temporal_samples()
// Single source of truth for shutter-window sample times, jitter, Halton,
// and normalized reconstruction-filter weights.
//
// PR1 — replaces the duplicated helpers that previously co-existed in:
//   * src/scene/camera/camera_v1/camera_motion_blur.cpp  // drift-allow: stale-ref
//       (CameraMotionBlurIntegrator::halton + sub_sample_times + filter_weight)
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
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/scene/model/camera/camera_common_types.hpp>  // TemporalSamplePattern, TemporalFilter

#include <cstdint>
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

} // namespace chronon3d::temporal
