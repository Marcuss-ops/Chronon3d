#pragma once

// include/chronon3d/simd/detail/avx2_kernels.hpp
// ════════════════════════════════════════════════════════════════════════════
// F5.2 first-kernel (TICKET-SIMD-VECTORIZE-KERNEL-SET-V1) —
// AVX2 implementation of the premultiplied-alpha "SRC_OVER" blend.
// Header-only inline pattern (matches
// `include/chronon3d/simd/detail/scalar_kernels.hpp`).
// ════════════════════════════════════════════════════════════════════════════
//
// Per AGENTS.md §regole "Cat-3 minimal-surface: prefer DELETE over
// WRAP", the SIMD body lands inline in this `detail/` header rather
// than as a TU-local definition in `src/backends/software/simd/`.
// This matches the F5.1 `scalar_kernels.hpp` precedent: the resolver
// factory `include/chronon3d/simd/kernel_resolver.hpp::kAvx2Set`
// references `&detail::avx2_composite_normal_premul` directly, with
// the fn-pointer slot bound at compile-time as an `inline constexpr`
// `PixelKernelSet` aggregator.
//
// Compliance:
//   - ZERO new public SDK API (the fn is in the `detail/` sub-namespace
//     and not exposed via `include/chronon3d/simd/pixel_kernels.hpp`).
//   - ZERO new third-party deps (`<immintrin.h>` is a COMPILER
//     intrinsic header, not a third-party lib; the AGENTS.md Rule 5
//     forbid list is `<msdfgen>`/`<libtess2>`/`<unicode[/...]>`).
//   - NO `-march=native` (compile-time `#if defined(__AVX2__)` only;
//     runtime ISA gating via `CpuCapabilities::has_avx2`).
//   - RE-USES canonical `BlendKernel::ApplyFn` ABI from F5.1 — no
//     new registry/resolver/cache per AGENTS.md "no singletons
//     senza ADR" (ADR-025 is the existing anchor).

#include <cstddef>

#include <chronon3d/simd/detail/scalar_kernels.hpp>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace chronon3d {
namespace simd {
namespace detail {

// ── avx2_composite_normal_premul ──────────────────────────────────────────
//
// ABI: matches `pixel_kernels.hpp::BlendKernel::ApplyFn`:
//   void (float* dst_rgba, const float* src_rgba, std::size_t pixel_count)
//
// Domain: premultiplied-alpha SRC_OVER (matches `scalar_blend`):
//   for each pixel i:
//     sa     = src[i].a
//     inv    = 1 - sa
//     dst[i] = src[i] + dst[i] * inv    (per RGBA channel)
//
// AVX2 2-pixel batch (8 floats per `__m256`):
//   - load 8 src + 8 dst floats
//   - extract per-pixel alpha: sa0 = src[3], sa1 = src[7]
//   - broadcast + blend to construct [sa0,sa0,sa0,sa0, sa1,sa1,sa1,sa1]
//   - inv = 1 - sa
//   - result = src + dst * inv
//   - store 8 floats back
//
// Per F5.1 + ADR-025 contract: scalar = reference, AVX2 = bit-identical
// OR within `kKernelEpsilon` (1 ULP float32 = FLT_EPSILON per
// `pixel_kernels.hpp::kKernelEpsilon` SSoT). The scalar remainder
// fallback guarantees bit-identical output for the 1-pixel tail.
//
// When the compiler lacks `__AVX2__`, the body compiles to a pure
// scalar reference (no instruction selection surprise). When the
// compiler has `__AVX2__` but the runtime CPU lacks AVX2, the
// `kAvx2Set` is NOT bound (the resolver routes to `kScalarSet` via
// the `target.has_avx2` runtime check in `kernel_resolver.hpp`).
inline void avx2_composite_normal_premul(float* dst_rgba, const float* src_rgba,
                                         std::size_t pixel_count) noexcept {
    if (pixel_count == 0) return;

#if defined(__AVX2__)
    // 2-pixel batch: 8 floats per __m256; aligned_end = N - (N % 2)
    constexpr std::size_t PIXELS_PER_ITER = 2;
    constexpr std::size_t FLOATS_PER_PIXEL = 4;
    const std::size_t aligned_end =
        (pixel_count / PIXELS_PER_ITER) * PIXELS_PER_ITER;

    const __m256 ones = _mm256_set1_ps(1.0f);

    for (std::size_t i = 0; i < aligned_end; i += PIXELS_PER_ITER) {
        const float* src_ptr = src_rgba + i * FLOATS_PER_PIXEL;
        float*       dst_ptr = dst_rgba + i * FLOATS_PER_PIXEL;

        const __m256 src = _mm256_loadu_ps(src_ptr);
        const __m256 dst = _mm256_loadu_ps(dst_ptr);

        // Per-pixel alpha broadcast: sa0 = src[3], sa1 = src[7].
        //   sa_dup0 = [sa0, sa0, sa0, sa0, sa0, sa0, sa0, sa0]
        //   sa_dup1 = [sa1, sa1, sa1, sa1, sa1, sa1, sa1, sa1]
        // Then blend: lanes 0-3 from sa_dup0, lanes 4-7 from sa_dup1.
        const __m256 sa_dup0 =
            _mm256_permutevar8x32_ps(src, _mm256_set1_epi32(3));
        const __m256 sa_dup1 =
            _mm256_permutevar8x32_ps(src, _mm256_set1_epi32(7));
        const __m256 sa =
            _mm256_blend_ps(sa_dup0, sa_dup1, 0xF0); // 0xF0 = lanes 0-3 from a, 4-7 from b

        const __m256 inv  = _mm256_sub_ps(ones, sa);
        const __m256 out  = _mm256_add_ps(src, _mm256_mul_ps(dst, inv));

        _mm256_storeu_ps(dst_ptr, out);
    }

    // Remainder (0 or 1 pixel): scalar fallback for bit-identical tail.
    scalar_blend(dst_rgba + aligned_end * FLOATS_PER_PIXEL,
                 src_rgba + aligned_end * FLOATS_PER_PIXEL,
                 pixel_count - aligned_end);
#else
    // No AVX2 in the compiler toolchain: pure scalar reference.
    scalar_blend(dst_rgba, src_rgba, pixel_count);
#endif
}

} // namespace detail
} // namespace simd
} // namespace chronon3d
