#pragma once
// ==============================================================================
// chronon3d/scene/model/camera/camera_common_types.hpp
//
// Lightweight camera types shared by the projection model and scene values.
//
// Contains: TemporalSamplePattern, TemporalFilter, DepthOfFieldSettings
// These are pure data types with no heavy dependencies.
// ==============================================================================

#include <chronon3d/core/types/types.hpp>

namespace chronon3d {

// ── Temporal sample pattern for motion blur ──────────────────────────────
enum class TemporalSamplePattern {
    Uniform,     // evenly spaced samples (no jitter)
    Stratified,  // one random point per equal-sized stratum
    Halton       // low-discrepancy Halton sequence (2,3)
};

// ── Temporal reconstruction filter ───────────────────────────────────────
enum class TemporalFilter {
    Box,         // equal weight for all samples (1/N)
    Triangle,    // linear ramp: highest weight at center, zero at edges
    Gaussian     // Gaussian bell curve (sigma = exposure_duration / 4)
};

struct DepthOfFieldSettings {
    bool  enabled{false};

    // Legacy simple model (backward compatible):
    //   blur = |layer_z - focus_z| * aperture, clamped to max_blur.
    f32   focus_z{0.0f};      // world-space Z at which blur = 0
    f32   aperture{0.015f};   // blur per unit of Z distance from focus
    f32   max_blur{24.0f};    // clamp: pixels of blur at extreme depths

    // Physical lens model: camera-space distance to focus plane (scene units).
    f32   focus_distance{1000.0f};
    bool  use_physical_model{false};

    // Near / far bokeh separation (for future iris shape rendering).
    f32   near_bokeh_radius{0.0f};
    f32   far_bokeh_radius{0.0f};

    // Physical lens shaping. No blur kernel implements these yet — see
    // reject_unsupported_dof_shape() in dof.hpp, which fails closed when
    // either is authored so shaped-bokeh requests can never render silently
    // as plain circular bokeh.
    std::uint32_t iris_blades{0}; // polygonal iris blade count; 0 = circular bokeh
    f32 highlight_gain{0.0f};     // out-of-focus highlight weighting; 0 = off
};

} // namespace chronon3d
