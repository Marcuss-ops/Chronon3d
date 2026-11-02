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
    // Camera-space distance to the focus plane (scene units); the only focus
    // coordinate consumed by the thin-lens CoC calculation.
    f32   focus_distance{1000.0f};
    f32   max_blur{24.0f};    // maximum CoC radius in pixels

    // Near / far bokeh separation (for future iris shape rendering).
    f32   near_bokeh_radius{0.0f};
    f32   far_bokeh_radius{0.0f};

    // Iris shaping is part of the camera contract and is rejected until a
    // kernel renders polygonal apertures. Circular-bokeh highlight weighting
    // is implemented by the software and Vulkan kernels.
    std::uint32_t iris_blades{0}; // polygonal iris blade count; 0 = circular bokeh
    f32 highlight_gain{0.0f};     // out-of-focus highlight weighting; 0 = off
};

} // namespace chronon3d
