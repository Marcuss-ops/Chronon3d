#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// matte.hpp — Track-matte helpers and SIMD kernel declarations for Chronon3D
//
// These functions apply alpha/luma coverage to a premultiplied target pixel.
// Input:  target (premultiplied RGBA), matte (premultiplied RGBA)
// Output: target modified in-place by coverage factor.
//
// The luma helper uses the premultiplied coefficients directly (no un-premultiply)
// because for a premultiplied fb, r = R*α, so 0.2126*r = 0.2126*R*α already
// includes the alpha factor — no need to multiply by α again.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/math/color.hpp>
#include <chronon3d/core/types/types.hpp>

namespace chronon3d {

// ── Coverage helpers ─────────────────────────────────────────────────────────

/// Compute alpha coverage from a premultiplied pixel.
inline float coverage_alpha(float matte_alpha) {
    return std::clamp(matte_alpha, 0.0f, 1.0f);
}

/// Compute inverted alpha coverage.
inline float coverage_alpha_inverted(float matte_alpha) {
    return std::clamp(1.0f - matte_alpha, 0.0f, 1.0f);
}

/// Compute luma coverage from a premultiplied pixel.
/// Uses the premultiplied luma weights directly so no extra α multiply.
inline float coverage_luma(const Color& matte) {
    return std::clamp(0.2126f * matte.r + 0.7152f * matte.g + 0.0722f * matte.b, 0.0f, 1.0f);
}

/// Compute inverted luma coverage.
inline float coverage_luma_inverted(const Color& matte) {
    return std::clamp(1.0f - (0.2126f * matte.r + 0.7152f * matte.g + 0.0722f * matte.b), 0.0f, 1.0f);
}

/// Apply a coverage factor to a premultiplied Color in-place.
inline void apply_coverage(Color& target, float coverage) {
    const float c = std::clamp(coverage, 0.0f, 1.0f);
    target.r *= c;
    target.g *= c;
    target.b *= c;
    target.a *= c;
}

} // namespace chronon3d

// ── SIMD declarations ────────────────────────────────────────────────────────
// These process contiguous scan-line segments.

namespace chronon3d::simd {

/// Apply alpha matte coverage to a contiguous run of target pixels.
/// `target` and `matte` point to premultiplied Color arrays of length pixel_count.
/// Each target pixel is multiplied by the alpha of the corresponding matte pixel.
void apply_alpha_matte_premul(
    Color* __restrict__ target,
    const Color* __restrict__ matte,
    int pixel_count,
    bool inverted
);

/// Apply luma matte coverage to a contiguous run of target pixels.
/// Uses the weighted luma of the premultiplied matte pixel directly.
void apply_luma_matte_premul(
    Color* __restrict__ target,
    const Color* __restrict__ matte,
    int pixel_count,
    bool inverted
);

} // namespace chronon3d::simd
