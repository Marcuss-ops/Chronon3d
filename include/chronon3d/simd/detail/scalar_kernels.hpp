#pragma once

// ── detail/scalar_kernels — scalar reference implementations ─────────────
//
// The 5 canonical scalar fn-pointers that `PixelKernelSet` binds by
// default. Per user-spec verbatim "scalar = reference", these bodies
// ARE the canonical-by-construction reference for the
// bit-identical-or-ε-bounded SIMD contract documented in
// `include/chronon3d/simd/pixel_kernels.hpp`.
//
// The 5 kernels correspond 1:1 to the 5 user-spec kernel families:
//   - `scalar_blur`         (BlurKernel)
//   - `scalar_blend`        (BlendKernel)
//   - `scalar_glow`         (GlowKernel)
//   - `scalar_resample`     (ResampleKernel)
//   - `scalar_color_matrix` (ColorMatrixKernel)
//
// Implementation strategy: portable scalar loops over RGBA premul
// pixel spans. No SIMD intrinsics, no Highway include, no platform
// headers. The bodies are TU-local `constexpr`-friendly so the
// resolver static tables can be initialized at compile-time for the
// `Scalar` CpuIsa and never touch the global state.
//
// SIMD counterparts (AVX2 / SSE42 / AVX512 / NEON) are forward-
// declared in `pixel_kernels.hpp::ApplyFn` slots and bound from
// `src/backends/software/simd/highway_*_kernels.cpp` after migration
// (TICKET-SIMD-REGISTRY-IMPLEMENTATION).

#include <cstdint>
#include <cstddef>

#include <chronon3d/simd/pixel_kernels.hpp>

namespace chronon3d {
namespace simd {
namespace detail {

// ── Helper: clamp float to [0.0, 1.0] ────────────────────────────────────
inline float clamp01(float v) noexcept {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// ── Blur (scalar reference) ─────────────────────────────────────────────
//
// Horizontal separable box blur over RGBA premul pixels.
// `radius` ∈ [0.0f, 64.0f]; clamped silently inside the loop. The
// scaffold body implements a real parameterized box blur (kernel width
// = 2*floor(radius)+1, edge-clamped) so the reference DOES perform the
// operation its name advertises per user-spec verbatim
// "scalar = reference". Full quality tuning (separable 2-pass H/V with
// kernel-cache reordering) lands in
// `src/backends/software/simd/highway_blend_kernels.cpp` post-migration
// (forward-point TICKET-SIMD-REGISTRY-IMPLEMENTATION).
inline void scalar_blur(float* dst_rgba, const float* src_rgba,
                        std::size_t pixel_count, float radius) noexcept {
    if (pixel_count == 0) return;
    if (radius < 0.0f) radius = 0.0f;
    if (radius > 64.0f) radius = 64.0f;
    const int half = static_cast<int>(radius);
    if (half == 0) {
        // radius=0: identity copy (no blur)
        for (std::size_t i = 0; i < pixel_count * 4; ++i) {
            dst_rgba[i] = src_rgba[i];
        }
        return;
    }
    const int kernel_size = 2 * half + 1;
    const float weight = 1.0f / static_cast<float>(kernel_size);
    const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(pixel_count) - 1;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        for (std::size_t k = 0; k < 4; ++k) {
            float sum = 0.0f;
            for (int j = -half; j <= half; ++j) {
                std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(i) + j;
                if (idx < 0) idx = 0;
                if (idx > last) idx = last;
                sum += src_rgba[4 * idx + k];
            }
            dst_rgba[4 * i + k] = sum * weight;
        }
    }
}

// ── Blend (scalar reference) ────────────────────────────────────────────
//
// Premultiplied-alpha "SRC_OVER":
//   dst.rgb = src.rgb + dst.rgb * (1 - src.a)
//   dst.a   = src.a   + dst.a   * (1 - src.a)
// Matches the existing `composite_normal_premul` reference for ABI
// continuity; the other Porter-Duff / Photoshop modes (Add / Multiply /
// Screen / Overlay / Darken / Lighten / Difference / Exclusion / etc.)
// plug in as additional `BlendKernel` overloads in extended tables
// (forward-point TICKET-SIMD-REGISTRY-IMPLEMENTATION).
inline void scalar_blend(float* dst_rgba, const float* src_rgba,
                         std::size_t pixel_count) noexcept {
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const float sa = src_rgba[4 * i + 3];
        const float inv = 1.0f - sa;
        dst_rgba[4 * i + 0] = src_rgba[4 * i + 0] + dst_rgba[4 * i + 0] * inv;
        dst_rgba[4 * i + 1] = src_rgba[4 * i + 1] + dst_rgba[4 * i + 1] * inv;
        dst_rgba[4 * i + 2] = src_rgba[4 * i + 2] + dst_rgba[4 * i + 2] * inv;
        dst_rgba[4 * i + 3] = sa + dst_rgba[4 * i + 3] * inv;
    }
}

// ── Glow (scalar reference) ─────────────────────────────────────────────
//
// Per-pixel emissive add: dst.rgb += src.rgb * intensity (clamped to
// [0.0, 1.0] post-add). Alpha untouched (matches glow bloom contract).
inline void scalar_glow(float* dst_rgba, const float* src_rgba,
                        std::size_t pixel_count, float intensity) noexcept {
    for (std::size_t i = 0; i < pixel_count; ++i) {
        dst_rgba[4 * i + 0] = clamp01(dst_rgba[4 * i + 0] + src_rgba[4 * i + 0] * intensity);
        dst_rgba[4 * i + 1] = clamp01(dst_rgba[4 * i + 1] + src_rgba[4 * i + 1] * intensity);
        dst_rgba[4 * i + 2] = clamp01(dst_rgba[4 * i + 2] + src_rgba[4 * i + 2] * intensity);
        // alpha untouched per glow bloom contract
    }
}

// ── Resample (scalar reference) ─────────────────────────────────────────
//
// Bilinear resample over RGBA premul row span with single-axis weight.
// 2-pass (horizontal then vertical) usage is canonical; the resolver
// hands out the same fn-pointer for both axes.
inline void scalar_resample(float* dst_rgba, std::size_t dst_pitch,
                            const float* src_rgba, std::size_t src_pitch,
                            std::size_t pixel_count, float weight) noexcept {
    const float w = clamp01(weight);
    const float inv = 1.0f - w;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        for (std::size_t k = 0; k < 4; ++k) {
            const float a = src_rgba[i * src_pitch + k];
            const float b = src_rgba[(i + 1) * src_pitch + k];
            dst_rgba[i * dst_pitch + k] = a * inv + b * w;
        }
    }
}

// ── ColorMatrix (scalar reference) ──────────────────────────────────────
//
// 3×4 RGBA transform (row-major): `dst[rgba] = M[3x4] * src[rgba, 1]`.
// `matrix3x4` is 12 floats: row 0 = R coefficients, row 1 = G, row 2 = B,
// row 3 = A. Each row has 4 floats (RGB-in + bias, G,B-out offsets & A).
// Scaffold body: minimal linear transform; full pipeline lands in the
// IMPL ticket.
inline void scalar_color_matrix(float* dst_rgba, const float* src_rgba,
                                std::size_t pixel_count,
                                const float* matrix3x4) noexcept {
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const float sr = src_rgba[4 * i + 0];
        const float sg = src_rgba[4 * i + 1];
        const float sb = src_rgba[4 * i + 2];
        const float sa = src_rgba[4 * i + 3];
        // Output channel 0 = M[0][0]*sr + M[0][1]*sg + M[0][2]*sb + M[0][3]*sa
        dst_rgba[4 * i + 0] = matrix3x4[0] * sr + matrix3x4[1] * sg + matrix3x4[2] * sb + matrix3x4[3] * sa;
        dst_rgba[4 * i + 1] = matrix3x4[4] * sr + matrix3x4[5] * sg + matrix3x4[6] * sb + matrix3x4[7] * sa;
        dst_rgba[4 * i + 2] = matrix3x4[8] * sr + matrix3x4[9] * sg + matrix3x4[10] * sb + matrix3x4[11] * sa;
        dst_rgba[4 * i + 3] = sa; // alpha passthrough at scaffold
    }
}

} // namespace detail
} // namespace simd
} // namespace chronon3d
