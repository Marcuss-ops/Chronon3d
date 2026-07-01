#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>

#include <filesystem>
#include <string>
#include <cmath>

namespace chronon3d::test {

// ── Thresholds ──────────────────────────────────────────────────────────────
// Default thresholds match the previous camera-specific defaults (converted to
// linear-space floats where appropriate).

struct ImageDiffThreshold {
    // Mean absolute error across all four channels, in [0,1].
    double max_mean_abs_error{1.5 / 255.0};

    // Maximum absolute error of any single channel, in [0,1].
    double max_abs_error{18.0 / 255.0};

    // Ratio of pixels where any channel exceeds max_abs_error.
    double max_changed_pixel_ratio{0.015};

    // Root-mean-square error, per channel, in [0,1].
    double max_rmse{2.0 / 255.0};

    // Minimum acceptable SSIM (Structural Similarity Index) in [0,1].
    // 1.0 = identical, 0.98 = near-lossless perceptual match.
    double min_ssim{0.98};
};

// ── Result ──────────────────────────────────────────────────────────────────

struct ImageDiffResult {
    double mean_abs_error{0.0};
    double max_abs_error{0.0};
    double changed_pixel_ratio{0.0};
    double rmse{0.0};
    double psnr{0.0};

    // SSIM (Structural Similarity Index) in [0, 1].
    // 1.0 = identical; computed on luminance using 8×8 blocks.
    double ssim{1.0};

    bool   passed{false};
    std::string report;
};

// ── Domain of comparison ────────────────────────────────────────────────────
//
//   Rendered framebuffer: linear float RGBA
//   Golden PNG:            sRGB 8-bit RGBA  (loaded as float [0,1])
//
//   Comparison:            sRGB for RGB, linear for alpha.
//
//   Therefore `actual` pixels are converted to sRGB via to_srgb() before
//   comparison against the golden (which is already sRGB).

// ── compare_framebuffers ────────────────────────────────────────────────────
// Compare two framebuffers pixel-by-pixel.  `actual` is expected to be linear
// float and will be converted to sRGB before comparison.  `expected` is
// compared directly (assumed to be sRGB).
//
// Returns diff metrics and a passed/failed verdict against the thresholds.

ImageDiffResult compare_framebuffers(
    const Framebuffer& actual,
    const Framebuffer& expected,
    const ImageDiffThreshold& threshold = {}
);

// ── create_diff_heatmap ────────────────────────────────────────────────────
// Produce a visual-diff framebuffer where:
//   - matching pixels   → dim semi-transparent (grey/black)
//   - differing pixels  → highlighted in red proportional to error
//
// Useful for debugging test failures.  The returned framebuffer is linear
// float and can be saved with save_png().

Framebuffer create_diff_heatmap(
    const Framebuffer& actual,
    const Framebuffer& expected,
    const ImageDiffThreshold& threshold = {}
);

// ── compute_ssim ────────────────────────────────────────────────────────────
// Compute the Structural Similarity Index between two sRGB framebuffers.
//
// Uses 8×8 non-overlapping blocks on the luminance (luma) channel.
// Constants C1 = (0.01)², C2 = (0.03)² (standard for 8-bit [0,1] range).
//
// Both framebuffers must have identical dimensions.

double compute_ssim(
    const Framebuffer& a,
    const Framebuffer& b
);

} // namespace chronon3d::test
