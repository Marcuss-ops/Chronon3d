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
resolve_pixel_kernels(const CpuCapabilities& /*target*/) noexcept {
    // SCAFFOLD: unconditional Scalar route. Per-ISA switch landed by
    // TICKET-SIMD-REGISTRY-IMPLEMENTATION §Step 1.
    return kScalarSet;
}

} // namespace simd
} // namespace chronon3d
