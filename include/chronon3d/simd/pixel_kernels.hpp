#pragma once

// ── pixel_kernels — canonical PixelKernelSet + 5 kernel families ────────
//
// Single source of truth for the per-kernel ABI that the
// `resolve_pixel_kernels(const CpuCapabilities&)` factory picks from.
// Per AGENTS.md "Cat-3 minimal: prefer DELETE over WRAP", this struct
// REPLACES the 48 HWY_DYNAMIC_DISPATCH wrappers across the existing
// 6 `src/backends/software/simd/highway_*_kernels.cpp` files (forward-
// point TICKET-SIMD-REGISTRY-IMPLEMENTATION).
//
// Design rationale (ADR-025):
//   - Each kernel is a VALUE TYPE carrying a function-pointer slot
//     (zero-overhead vtable dispatch at call-site, no std::function
//     indirection, ABIs-stable, no global mutable cache).
//   - The 5 scalar fn-pointers are defined in
//     `include/chronon3d/simd/detail/scalar_kernels.hpp` (the
//     reference; matches scalar output for the bit-identical-or-ε-bounded
//     contract below).
//   - SIMD fn-pointers are forward-declared here; bodies land in
//     `src/backends/software/simd/highway_*_kernels.cpp` after
//     migration (TICKET-SIMD-REGISTRY-IMPLEMENTATION).
//   - The resolver returns `const PixelKernelSet&` (stateless static
//     per CpuIsa), NOT a singleton cache (per AGENTS.md "no singletons").
//
// ABI contract: scalar = reference, SIMD = bit-identical or within
// `kKernelEpsilon` (1 ULP float32 = 1.19e-7). The epsilon bound is
// documented once in this header as SSoT — no per-call-site docs.

#include <cfloat>     // FLT_EPSILON (1 ULP float32, IEEE-754 exact)
#include <cstdint>
#include <cstddef>

#include <chronon3d/simd/cpu_isa.hpp>   // forward-decl CpuIsa + CpuCapabilities

namespace chronon3d {
namespace simd {

// ── ABI epsilon bound (canonical SSoT) ────────────────────────────────────
///
/// SIMD vs scalar divergence is ε-bounded by 1 ULP float32 per kernel.
/// Cross-ISA parity (AVX2 vs NEON) inherits the same bound. Per
/// ADR-025 §Decision 3, "bit-identico" is theoretically impossible
/// across lane widths (8-way vs 4-way vs 32-way); this constant
/// codifies the certified bound once. Consumers MUST NOT introduce a
/// second `kKernelEpsilon` definition. `FLT_EPSILON` is the IEEE-754
/// exact 1 ULP float32 constant from `<cfloat>` (avoids the 3-sig-fig
/// rounding artifact of a literal `1.19e-7f`).
static constexpr float kKernelEpsilon = FLT_EPSILON; // 1 ULP float32 (IEEE-754 exact)

// ── Blur kernel ──────────────────────────────────────────────────────────
///
/// Domain: image-space blur (separable horizontal + vertical passes);
/// templated on the box radius. ABI signature: span-of-float-4 pixels
/// (RGBA interleaved, premultiplied) — matches the existing
/// `composite_normal_premul` family for ABI consistency.
struct BlurKernel {
    /// horizontal pass: row-major RGBA premul, `pixel_count` pixels
    using ApplyFn = void (*)(float* dst_rgba, const float* src_rgba,
                             std::size_t pixel_count, float radius);

    /// Default ctor: binds the canonical SCALAR reference (always set;
    /// SIMD slots move to nullptr pending TICKET-SIMD-REGISTRY-IMPLEMENTATION).
    BlurKernel() noexcept;

    /// Ctor with explicit fn-pointer (used by the resolver factory).
    constexpr explicit BlurKernel(ApplyFn apply) noexcept : apply(apply) {}

    ApplyFn apply;
};

// ── Blend kernel (premultiplied alpha composite modes) ──────────────────
///
/// Domain: per-pixel blend ops across the whole "Porter-Duff +
/// Photoshop" matrix. ABI: span<Color>-style (matches existing
/// `composite_*_premul` functions in `include/chronon3d/simd/kernels.hpp`).
struct BlendKernel {
    using ApplyFn = void (*)(float* dst_rgba, const float* src_rgba,
                             std::size_t pixel_count);

    constexpr explicit BlendKernel(ApplyFn apply) noexcept : apply(apply) {}

    ApplyFn apply;
};

// ── Glow kernel ──────────────────────────────────────────────────────────
///
/// Domain: per-pixel glow accumulation (bloom-style emissive add).
/// ABI: same as BlendKernel (RGBA premul, span-of-pixels).
struct GlowKernel {
    using ApplyFn = void (*)(float* dst_rgba, const float* src_rgba,
                             std::size_t pixel_count, float intensity);

    constexpr explicit GlowKernel(ApplyFn apply) noexcept : apply(apply) {}

    ApplyFn apply;
};

// ── Resample kernel (bilinear / bicubic / Lanczos) ──────────────────────
///
/// Domain: image-space resampling for rotated / scaled sprites.
/// ABI: 2 separate row + col slots to avoid recomputing weights per axis.
struct ResampleKernel {
    using ApplyFn = void (*)(float* dst_rgba, std::size_t dst_pitch,
                             const float* src_rgba, std::size_t src_pitch,
                             std::size_t pixel_count, float weight);

    constexpr explicit ResampleKernel(ApplyFn apply) noexcept : apply(apply) {}

    ApplyFn apply;
};

// ── ColorMatrix kernel (3x4 RGBA transform) ─────────────────────────────
///
/// Domain: per-pixel color conversion (YUV→RGB, RGBA→PRGB32, etc.).
/// ABI: span-style for row-major accesses.
struct ColorMatrixKernel {
    using ApplyFn = void (*)(float* dst_rgba, const float* src_rgba,
                             std::size_t pixel_count, const float* matrix3x4);

    constexpr explicit ColorMatrixKernel(ApplyFn apply) noexcept
        : apply(apply) {}

    ApplyFn apply;
};

// ── PixelKernelSet aggregator ──────────────────────────────────────────
///
/// ABI-aligned set of the 5 kernel families per
/// `include/chronon3d/simd/pixel_kernels.hpp` user-spec verbatim.
/// The struct is trivially-copyable (single fn-pointer per slot) so
/// `const PixelKernelSet&` resolution is zero-copy at call-site.
struct PixelKernelSet {
    BlurKernel        blur;
    BlendKernel       blend;
    GlowKernel        glow;
    ResampleKernel    resample;
    ColorMatrixKernel color_matrix;

    // NOTE: Per AGENTS.md §regola "Cat-3 minimal-surface: prefer DELETE
    // over WRAP", `isa` member field is INTENTIONALLY absent. The
    // resolver routes the correct static at construction-time; consumers
    // never check `set.isa == caps.highest_isa` — they just call. Any
    // diagnostic logging of the bound ISA must use
    // `caps.highest_isa` (the input) instead of `set.isa`.
};

// ── Resolution rule ────────────────────────────────────────────────────
//
// `resolve_pixel_kernels` returns `const PixelKernelSet&` to one of
// 5 STATIC const tables — one per `CpuIsa` enumerator. The CpuIsa-keyed
// dispatch is implemented in `kernel_resolver.hpp`.
//
// The `const PixelKernelSet&` return type encodes "stateless reference
// to a per-ISA static" — NOT a singleton cache. The 5 statics are
// constructed once at program init (no lazy init, no global mutable
// counter). AGENTS.md "no singletons senza ADR" is honored because
// no per-construction cache state lives across calls.
//
// See `kernel_resolver.hpp` for the entry-point signatures.

} // namespace simd
} // namespace chronon3d
