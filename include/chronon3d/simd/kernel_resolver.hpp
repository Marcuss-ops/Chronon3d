#pragma once

// ── kernel_resolver — SIMD kernel dispatch factory ─────────────────────
//
// Single entry point that picks the right `PixelKernelSet` static for a
// given `CpuCapabilities`. The resolver is the CANONICAL replacement for
// the 48 HWY_DYNAMIC_DISPATCH wrappers across the 6 Highway TU files.
//
// Design rationale (ADR-025):
//   - The resolver returns `const PixelKernelSet&` — a reference to a
//     STATIC const table (one per `CpuIsa` enumerator). The 5 statics
//     are constructed once at program init (zero-cost steady-state).
//   - NO global mutable cache (rejected per ADR-025 §ALT-A "global
//     singleton cache"; per AGENTS.md "no nuovi singleton/registry/
//     resolver/cache senza ADR" — this header IS the ADR-anchor).
//   - NO std::function indirection (rejected per ADR-025 §ALT-B;
//     fn-pointer slot per ABI is zero-overhead).
//   - NO compile-time ISA tag dispatch (rejected per ADR-025 §ALT-C
//     "interceptor compile-time tag"; runtime dispatch wins because
//     the same binary must run on heterogeneous VPS hosts).
//
// The resolver is the dependency-injectable boundary — the existing
// 6 Highway TU files call `m_simd_kernels.<op>.apply(...)` instead of
// `HWY_DYNAMIC_DISPATCH(<op>)(...)` after the migration lands
// (forward-point TICKET-SIMD-REGISTRY-IMPLEMENTATION).

#include <chronon3d/simd/cpu_isa.hpp>
#include <chronon3d/simd/pixel_kernels.hpp>
#include <chronon3d/simd/detail/scalar_kernels.hpp>

#if defined(__AVX2__)
// F5.2 (TICKET-SIMD-VECTORIZE-KERNEL-SET-V1) first-kernel: AVX2
// premultiplied-alpha blend. The header-only inline pattern
// matches `scalar_kernels.hpp`; `kAvx2Set` below references
// `&detail::avx2_composite_normal_premul` directly. Future SIMD
// per-ISA headers (SSE42 / NEON / AVX512) follow the same pattern
// (forward-points b/h in the canonical ticket).
#include <chronon3d/simd/detail/avx2_kernels.hpp>
#endif

namespace chronon3d {
namespace simd {

/// Name of a `PixelKernelSet` (for diagnostics + gate audit logs).
///
/// Returns "scalar", "sse42", "avx2", "avx512", or "neon". Inverse of
/// `CpuIsa` → human-readable.
[[nodiscard]] const char* pixel_kernel_set_name(CpuIsa isa) noexcept;

// ── kScalarSet — the ALWAYS-AVAILABLE reference binding ─────────────────
///
/// `inline constexpr` `PixelKernelSet` bound to the 5 scalar reference
/// fn-pointers (`detail::scalar_*`). This is the canonical
/// ALWAYS-AVAILABLE static for the resolver. At scaffold time, the
/// resolver routes ALL `CpuIsa` requests to THIS static (the IMPL
/// ticket forward-points the per-ISA statics + SIMD bindings).
///
/// ABI: fn-pointer slots are `inline` function addresses (C++17
/// guarantees a single address per inline function across the
/// program — the constexpr binding is well-defined).
inline constexpr PixelKernelSet kScalarSet = PixelKernelSet{
    BlurKernel{&detail::scalar_blur},
    BlendKernel{&detail::scalar_blend},
    GlowKernel{&detail::scalar_glow},
    ResampleKernel{&detail::scalar_resample},
    ColorMatrixKernel{&detail::scalar_color_matrix},
};

#if defined(__AVX2__)
// ── kAvx2Set — AVX2-bound PixelKernelSet (F5.2 first-kernel) ──────────
//
// `inline constexpr` `PixelKernelSet` bound to:
//   - blend       → `&detail::avx2_composite_normal_premul` (AVX2)
//   - blur        → `&detail::scalar_blur`                  (fallback, F5.2 forward-point c)
//   - glow        → `&detail::scalar_glow`                  (fallback, forward-point d+e)
//   - resample    → `&detail::scalar_resample`              (fallback, forward-point e)
//   - color_matrix→ `&detail::scalar_color_matrix`          (fallback, forward-point e)
//
// ABI contract: AVX2 blend is bit-identical or within `kKernelEpsilon`
// (1 ULP float32) per `pixel_kernels.hpp::kKernelEpsilon` SSoT. The
// 4 fallback slots use the SAME scalar fn-pointers as `kScalarSet`,
// guaranteeing bit-identical output (so the parity test
// `tests/simd/test_simd_parity_blend.cpp` can detect AVX2-specific
// divergences ONLY on the blend slot).
inline constexpr PixelKernelSet kAvx2Set = PixelKernelSet{
    BlurKernel{&detail::scalar_blur},
    BlendKernel{&detail::avx2_composite_normal_premul},
    GlowKernel{&detail::scalar_glow},
    ResampleKernel{&detail::scalar_resample},
    ColorMatrixKernel{&detail::scalar_color_matrix},
};
#endif // __AVX2__

/// Resolve the canonical `PixelKernelSet` for the given CPU capabilities.
///
/// SCAFFOLD BEHAVIOR (F5.1 commit): routes ALL `CpuIsa` requests to
/// `kScalarSet` (the always-available reference). The per-ISA static
/// tables + SIMD-body bindings are forward-pointed to
/// TICKET-SIMD-REGISTRY-IMPLEMENTATION §Step-1. Post-impl, the resolver
/// routes:
///   - AVX512 → kAvx512Set   (forward-point)
///   - AVX2   → kAvx2Set     (forward-point)
///   - SSE42  → kSse42Set    (forward-point)
///   - NEON   → kNeonSet     (forward-point)
///   - else / fallback → kScalarSet (THIS COMMIT)
///
/// Returns a const reference to a `PixelKernelSet` static. Lifetime is
/// program-static. Caller MUST NOT cache or mutate the return value;
/// copy into a local `PixelKernelSet` if needed.
///
/// ABI contract: `set.<kernel>.apply(...)` MUST be either bit-identical
/// to the scalar reference, or within `kKernelEpsilon` (1 ULP float32,
/// IEEE-754 exact via `FLT_EPSILON`).
///
/// Thread-safety: const-ref to immutable statics → global-lock-free.
[[nodiscard]] inline const PixelKernelSet&
resolve_pixel_kernels(const CpuCapabilities& target) noexcept {
    // F5.2 (TICKET-SIMD-VECTORIZE-KERNEL-SET-V1) first-kernel: route
    // AVX2-capable hosts to `kAvx2Set` (which has the AVX2 blend +
    // scalar fallbacks for the other 4 slots). All non-AVX2 hosts
    // route to `kScalarSet` (the always-available reference).
    // Per-ISA expansion (SSE42 / AVX512 / NEON) lands in forward-point h
    // (TICKET-SIMD-VECTORIZE-SSE42-NEON-V1).
#if defined(__AVX2__)
    if (target.has_avx2) return kAvx2Set;
#endif
    return kScalarSet;
}

} // namespace simd
} // namespace chronon3d
