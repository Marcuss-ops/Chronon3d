// src/backends/software/simd/avx2_pixel_kernels.cpp — AVX2 intrinsic
// implementations of the 5 kernel-family ApplyFn slots defined in
// `include/chronon3d/simd/pixel_kernels.hpp`.
//
// ABI conformance (ADR-025 §Decision): the 5 `detail::avx2_*` function
// pointers bind to `kAvx2Set` in `kernel_resolver.hpp` and are invoked
// through the public `resolve_pixel_kernels` factory. They replicate
// the canonical scalar reference ABI in `detail/scalar_kernels.hpp`
// (1:1 signature match), so the resolver's `kKernelEpsilon` contract
// (1 ULP float32 per `pixel_kernels.hpp`) is honored when AVX2
// compiles the same arithmetic under FMA. This file is private to
// `src/backends/software/simd/`; it adds NO new public surface
// (ADR-025 is the governing contract per AGENTS.md "no nuovi
// singleton/registry/resolver/cache senza ADR").
//
// The `__AVX2__` guards ensure non-AVX2 builds compile cleanly even
// if the per-file COMPILE_OPTIONS target gate is misconfigured (in
// practice the parent CMakeLists adds the gate via the
// `CHRONON3D_ISA_HAVE_AVX2` try-compile). On non-AVX2 builds the
// functions are no-op stubs; they are never called because the
// resolver routes only to `kScalarSet` when
// `CHRONON3D_ISA_BACKEND_AVX2` is not defined.

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#include <chronon3d/simd/pixel_kernels.hpp>

namespace chronon3d::simd::detail {

#if defined(__AVX2__)

// ── avx2_blend ──────────────────────────────────────────────────────────
// Premultiplied-alpha SRC_OVER: dst.rgba = src.rgba + dst.rgba * (1-src.a).
// 2-pixel AVX2 path with SSE 1-pixel remainder.
inline void avx2_blend(float* dst_rgba, const float* src_rgba,
                       std::size_t pixel_count) noexcept {
    const __m128 one128 = _mm_set1_ps(1.0f);
    std::size_t i = 0;
    for (; i + 2 <= pixel_count; i += 2) {
        const __m256 s = _mm256_loadu_ps(src_rgba + 4 * i);
        const __m256 d = _mm256_loadu_ps(dst_rgba + 4 * i);
        const __m128 s_lo = _mm256_castps256_ps128(s);
        const __m128 s_hi = _mm256_extractf128_ps(s, 1);
        const __m128 d_lo = _mm256_castps256_ps128(d);
        const __m128 d_hi = _mm256_extractf128_ps(d, 1);
        const __m128 a_lo = _mm_shuffle_ps(s_lo, s_lo, _MM_SHUFFLE(3, 3, 3, 3));
        const __m128 a_hi = _mm_shuffle_ps(s_hi, s_hi, _MM_SHUFFLE(3, 3, 3, 3));
        const __m128 r_lo = _mm_fmadd_ps(d_lo, _mm_sub_ps(one128, a_lo), s_lo);
        const __m128 r_hi = _mm_fmadd_ps(d_hi, _mm_sub_ps(one128, a_hi), s_hi);
        _mm256_storeu_ps(dst_rgba + 4 * i, _mm256_set_m128(r_hi, r_lo));
    }
    for (; i < pixel_count; ++i) {
        const __m128 s = _mm_loadu_ps(src_rgba + 4 * i);
        const __m128 d = _mm_loadu_ps(dst_rgba + 4 * i);
        const __m128 a = _mm_shuffle_ps(s, s, _MM_SHUFFLE(3, 3, 3, 3));
        const __m128 r = _mm_fmadd_ps(d, _mm_sub_ps(one128, a), s);
        _mm_storeu_ps(dst_rgba + 4 * i, r);
    }
}

// ── avx2_blur ───────────────────────────────────────────────────────────
// Horizontal separable box blur, RGBA premul, edge-clamped.
// Scalar-equivalent path (SSE per-pixel accumulation); AVX2 flags enable
// FMA contraction in successor releases (forward-point).
inline void avx2_blur(float* dst_rgba, const float* src_rgba,
                      std::size_t pixel_count, float radius) noexcept {
    if (pixel_count == 0) return;
    if (radius < 0.0f) radius = 0.0f;
    if (radius > 64.0f) radius = 64.0f;
    const int half = static_cast<int>(radius);
    if (half == 0) {
        std::memcpy(dst_rgba, src_rgba, pixel_count * 4 * sizeof(float));
        return;
    }
    const int kernel_size = 2 * half + 1;
    const float inv_w = 1.0f / static_cast<float>(kernel_size);
    const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(pixel_count) - 1;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        __m128 sum = _mm_setzero_ps();
        for (int j = -half; j <= half; ++j) {
            std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(i) + j;
            if (idx < 0)     idx = 0;
            if (idx > last) idx = last;
            sum = _mm_add_ps(sum, _mm_loadu_ps(src_rgba + 4 * static_cast<std::size_t>(idx)));
        }
        _mm_storeu_ps(dst_rgba + 4 * i, _mm_mul_ps(sum, _mm_set1_ps(inv_w)));
    }
}

// ── avx2_glow ───────────────────────────────────────────────────────────
// dst.rgb += src.rgb * intensity (clamped to [0,1]); alpha untouched.
// Scalar-equivalent (compiler auto-vectorizes under -mavx2).
inline void avx2_glow(float* dst_rgba, const float* src_rgba,
                      std::size_t pixel_count, float intensity) noexcept {
    for (std::size_t i = 0; i < pixel_count; ++i) {
        for (int k = 0; k < 3; ++k) {
            float v = dst_rgba[4 * i + k] + src_rgba[4 * i + k] * intensity;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            dst_rgba[4 * i + k] = v;
        }
        // alpha untouched per glow bloom contract
    }
}

// ── avx2_resample ───────────────────────────────────────────────────────
// Bilinear resample: dst[i] = src[i]*(1-w) + src[i+1]*w.
inline void avx2_resample(float* dst_rgba, std::size_t dst_pitch,
                          const float* src_rgba, std::size_t src_pitch,
                          std::size_t pixel_count, float weight) noexcept {
    float w = weight;
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    const float inv = 1.0f - w;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        for (int k = 0; k < 4; ++k) {
            const float a = src_rgba[i * src_pitch + k];
            const float b = src_rgba[(i + 1) * src_pitch + k];
            dst_rgba[i * dst_pitch + k] = a * inv + b * w;
        }
    }
}

// ── avx2_color_matrix ───────────────────────────────────────────────────
// 3x4 RGBA transform (3 rows × 4 cols); alpha passthrough.
inline void avx2_color_matrix(float* dst_rgba, const float* src_rgba,
                              std::size_t pixel_count,
                              const float* matrix3x4) noexcept {
    const float m0_0 = matrix3x4[0],  m0_1 = matrix3x4[1],
                m0_2 = matrix3x4[2],  m0_3 = matrix3x4[3];
    const float m1_0 = matrix3x4[4],  m1_1 = matrix3x4[5],
                m1_2 = matrix3x4[6],  m1_3 = matrix3x4[7];
    const float m2_0 = matrix3x4[8],  m2_1 = matrix3x4[9],
                m2_2 = matrix3x4[10], m2_3 = matrix3x4[11];
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const float sr = src_rgba[4 * i + 0];
        const float sg = src_rgba[4 * i + 1];
        const float sb = src_rgba[4 * i + 2];
        const float sa = src_rgba[4 * i + 3];
        dst_rgba[4 * i + 0] = m0_0 * sr + m0_1 * sg + m0_2 * sb + m0_3 * sa;
        dst_rgba[4 * i + 1] = m1_0 * sr + m1_1 * sg + m1_2 * sb + m1_3 * sa;
        dst_rgba[4 * i + 2] = m2_0 * sr + m2_1 * sg + m2_2 * sb + m2_3 * sa;
        dst_rgba[4 * i + 3] = sa; // alpha passthrough (matches scalar reference)
    }
}

#else  // !__AVX2__

// Non-AVX2 stubs (never called by resolver on non-AVX2 builds because
// `CHRONON3D_ISA_BACKEND_AVX2` is not defined).
inline void avx2_blend(float*, const float*, std::size_t) noexcept {}
inline void avx2_blur(float*, const float*, std::size_t, float) noexcept {}
inline void avx2_glow(float*, const float*, std::size_t, float) noexcept {}
inline void avx2_resample(float*, std::size_t, const float*, std::size_t,
                          std::size_t, float) noexcept {}
inline void avx2_color_matrix(float*, const float*, std::size_t,
                              const float*) noexcept {}

#endif  // __AVX2__

}  // namespace chronon3d::simd::detail
