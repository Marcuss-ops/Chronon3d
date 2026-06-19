#pragma once
// ==============================================================================
// chronon3d/scene/model/camera/camera_common_types.hpp
//
// Lightweight types used by both camera_rig.hpp (Core) and camera_2_5d.hpp
// (Feature) so that camera_rig.hpp doesn't need the full camera_2_5d.hpp.
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
};

} // namespace chronon3d
