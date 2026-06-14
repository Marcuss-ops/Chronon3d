#pragma once

#include "cinematic_motion_scenes.hpp"
#include "../support/image_diff.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <tests/helpers/pixel_assertions.hpp>

#include <vector>
#include <cmath>

namespace chronon3d::test {

// ── Thresholds ──────────────────────────────────────────────────────────────

inline ImageDiffThreshold cinematic_motion_threshold() {
    ImageDiffThreshold t;
    t.max_mean_abs_error = 0.5 / 255.0;
    t.max_abs_error = 8.0 / 255.0;
    t.max_changed_pixel_ratio = 0.002;
    return t;
}

// ── Sub-frame comb metrics ──────────────────────────────────────────────────

struct SubframeCombMetrics {
    int unique_centers{0};
    float minimum_spacing_px{0.0f};
    float maximum_position_error_px{0.0f};
    float spacing_cv{0.0f};  // coefficient of variation, 0 = perfect uniform
    std::vector<float> positions_x;
    bool all_distinct{false};
    bool monotonic{false};
    bool spacing_uniform{false};
};

/// Analyze 8 markers on a framebuffer.  Each marker is identified by its
/// dominant-R color (red/orange/yellow/green/cyan/blue/purple/magenta).
/// Returns metric summary.
SubframeCombMetrics analyze_subframe_comb(const Framebuffer& fb);

// ── Contact sheet metrics ───────────────────────────────────────────────────

struct ContactSheetMetrics {
    float max_velocity_jump_px{0.0f};
    float max_acceleration_jump_px{0.0f};
    std::vector<float> centroid_x_per_panel;
    bool velocity_smooth{false};
    bool acceleration_smooth{false};
};

/// Given 9 framebuffers (one per panel), compute the dominant-object centroid
/// in each and check velocity/acceleration smoothness.
ContactSheetMetrics analyze_contact_sheet(
    const std::vector<std::shared_ptr<Framebuffer>>& panels);

// ── Cache parity metrics ────────────────────────────────────────────────────

struct CacheParityMetrics {
    double pixel_changed_ratio_top{0.0};   // cache OFF vs ON (should be 0)
    double pixel_changed_ratio_bot{0.0};   // version A vs B (should be > 0)
    bool cache_hit_identical{false};
    bool version_change_detected{false};
    bool version_change_localised{false};   // changes only in affected region
};

/// Compare top quads (should be identical) and bottom quads (should differ).
CacheParityMetrics analyze_cache_parity(
    const std::vector<std::shared_ptr<Framebuffer>>& quads);

// ── Bezier handles metrics ──────────────────────────────────────────────────

struct BezierHandlesMetrics {
    float p0_error{0.0f};
    float p3_error{0.0f};
    float tangent_start_dot{0.0f};  // dot(tangent(0), normalize(P1-P0))
    float tangent_end_dot{0.0f};    // dot(tangent(1), normalize(P3-P2))
    bool endpoints_match{false};
    bool tangents_align{false};
};

BezierHandlesMetrics analyze_bezier_handles(const Framebuffer& fb);

// ── Arc-length spacing metrics ──────────────────────────────────────────────

struct ArcLengthSpacingMetrics {
    float parametric_cv{0.0f};
    float arc_length_cv{0.0f};
    float parametric_max_min_ratio{0.0f};
    float arc_length_max_min_ratio{0.0f};
    bool arc_length_more_uniform{false};
};

/// Compare 33 markers in upper row (parametric) vs lower row (arc-length).
ArcLengthSpacingMetrics analyze_arc_length_spacing(const Framebuffer& fb);

// ── Temporal/spatial separation metrics ─────────────────────────────────────

struct TemporalSpatialSeparationMetrics {
    // The geometry hash should be identical across timing changes.
    // We approximate with centroid positions.
    float geometry_drift_top_vs_bot{0.0f};    // should be 0 (bezier path same)
    float timing_effect_linear_vs_ease{0.0f}; // should be > 0
    bool geometry_preserved{false};
    bool timing_affects_markers{false};
};

TemporalSpatialSeparationMetrics analyze_temporal_spatial_separation(
    const Framebuffer& fb);

// ── Coefficient of variation helper ─────────────────────────────────────────

inline float coefficient_of_variation(const std::vector<float>& values) {
    if (values.size() < 2) return 0.0f;
    float sum = 0.0f, sum_sq = 0.0f;
    for (float v : values) {
        sum += v;
        sum_sq += v * v;
    }
    float mean = sum / static_cast<float>(values.size());
    float variance = sum_sq / static_cast<float>(values.size()) - mean * mean;
    if (variance < 0.0f) variance = 0.0f;
    float stddev = std::sqrt(variance);
    if (mean < 0.0001f) return 0.0f;
    return stddev / mean;
}

/// Compute mean absolute per-pixel difference between two framebuffers.
inline double mean_abs_diff(const Framebuffer& a, const Framebuffer& b) {
    const int w = std::min(a.width(), b.width());
    const int h = std::min(a.height(), b.height());
    if (w <= 0 || h <= 0) return 1.0;
    double total = 0.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color ca = a.get_pixel(x, y);
            Color cb = b.get_pixel(x, y);
            total += std::abs(ca.r - cb.r) + std::abs(ca.g - cb.g)
                   + std::abs(ca.b - cb.b) + std::abs(ca.a - cb.a);
        }
    }
    return total / (w * h * 4.0);
}

/// Compute the ratio of pixels that differ beyond a threshold.
inline double changed_pixel_ratio(const Framebuffer& a, const Framebuffer& b,
                                   float threshold = 0.01f) {
    const int w = std::min(a.width(), b.width());
    const int h = std::min(a.height(), b.height());
    if (w <= 0 || h <= 0) return 1.0;
    int changed = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color ca = a.get_pixel(x, y);
            Color cb = b.get_pixel(x, y);
            if (std::abs(ca.r - cb.r) > threshold ||
                std::abs(ca.g - cb.g) > threshold ||
                std::abs(ca.b - cb.b) > threshold) {
                ++changed;
            }
        }
    }
    return static_cast<double>(changed) / (w * h);
}

} // namespace chronon3d::test
