#pragma once

// ── cpu_isa — canonical CPU ISA enumeration + capability detection ───────
//
// Single source of truth for the 5 ISAs that Chronon3D's SIMD kernel
// table (`include/chronon3d/simd/pixel_kernels.hpp`) dispatches over.
// Per AGENTS.md §regole "no espansione API non necessaria", this enum's
// 5 enumerators are the ONLY-public CPU ISA classification in
// `include/chronon3d/` — no `<cpuid.h>`, no platform headers, no NEON
// direct intrinsics leak into the public surface.
//
// Contract (cross-link: docs/adr/ADR-025-simd-registry.md):
//   - `Scalar` is the ALWAYS-AVAILABLE reference; kernels on `Scalar`
//     are the bit-exact (or ε-bounded — see kKernelEpsilon) reference.
//   - `SSE42` / `AVX2` / `AVX512` are x86-64 ISAs queried via CPUID.
//   - `NEON` is the aarch64 ISA queried via HWCAP.
//   - Per-host detection is one-time at construction; the result is
//     immutable for the lifetime of the program (no hidden statics —
//     the value is members of `CpuCapabilities` passed by const-ref at
//     the resolver boundary).
//
// Forbidden includes (AGENTS.md Rule 5): no `<msdfgen>`, `<libtess2>`,
// `<unicode[/...]>` — none imported here. The ISA detection is
// platform-abstracted to honor Cat-3 minimal surface.

#include <cstdint>

namespace chronon3d {
namespace simd {

/// CPU ISA enumeration. The 5 enumerators are the public surface; any
/// new ISA must be ADDED here + DECLARED in
/// `include/chronon3d/simd/kernel_resolver.hpp::resolve_pixel_kernels`
/// (forward-point TICKET-SIMD-REGISTRY-V1).
enum class CpuIsa {
    Scalar,   ///< Portable reference (always available)
    SSE42,    ///< x86-64 Streaming SIMD Extensions 4.2
    AVX2,     ///< x86-64 Advanced Vector Extensions 2
    AVX512,   ///< x86-64 Advanced Vector Extensions 512
    NEON,     ///< aarch64 ARM NEON
};

/// Name of a CpuIsa enumerator (lowercase; for diagnostics / env parsing).
///
/// Returns "scalar", "sse42", "avx2", "avx512", "neon". Stable wire
/// contract; do not re-order enumerators without updating this fn.
[[nodiscard]] const char* cpu_isa_name(CpuIsa isa) noexcept;

/// Parse a CpuIsa name. Returns false for unknown values.
///
/// Inverse of `cpu_isa_name(CpuIsa)`. Accepts lowercase; case-insensitive
/// to honor user ergonomics for the
/// `CHRONON3D_FORCE_CPU_ISA` env var.
[[nodiscard]] bool parse_cpu_isa(const char* text, CpuIsa& out) noexcept;

/// CPU capability snapshot. Immutable per-program-lifetime per session.
///
/// Anti-dup clause: this struct is the SINGLE public definition across
/// the entire SDK. The forward-declared siblings in
/// `pixel_kernels.hpp`/`kernel_resolver.hpp` consume this struct by
/// const-ref (never copy/bypass). DO NOT add a duplicate
/// `CpuCapabilities` definition anywhere in `include/chronon3d/` — the
/// gated `tools/check_architecture_boundaries.sh` rule #11 covers this.
struct CpuCapabilities {
    CpuIsa highest_isa;   ///< Best ISA the host supports (always ≥ Scalar).
    bool   has_sse42;     ///< x86-64 SSE4.2 available.
    bool   has_avx2;      ///< x86-64 AVX2 available.
    bool   has_avx512;    ///< x86-64 AVX-512 available.
    bool   has_neon;      ///< aarch64 NEON available.

    /// True if this capability snapshot supports at least the given ISA.
    /// Convenience for resolver dispatch: `if (caps.supports(CpuIsa::AVX2)) { ... }`.
    [[nodiscard]] bool supports(CpuIsa isa) const noexcept;
};

/// Detect CPU capabilities for the running host.
///
/// Honors `CHRONON3D_FORCE_CPU_ISA=Scalar|SSE42|AVX2|AVX512|NEON`
/// env var when set (for deterministic test reproducibility + post-
/// rot-recovery cross-checks per ADR-020 lineage). Defaults to host
/// detection when unset. Returns `Scalar` for the `highest_isa` field
/// when the host lacks all SIMD ISAs (the canonical safe fallback).
///
/// ABI-stable, noexcept, free of global mutable state — the result
/// lives in the returned value (per AGENTS.md "no singleton senza ADR";
/// see ADR-025 alternative-rejected §ALT-A global cache).
[[nodiscard]] CpuCapabilities detect_cpu_capabilities() noexcept;

} // namespace simd
} // namespace chronon3d
