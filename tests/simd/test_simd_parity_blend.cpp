// tests/simd/test_simd_parity_blend.cpp
// ════════════════════════════════════════════════════════════════════════════
// F5.2 first-kernel (TICKET-SIMD-VECTORIZE-KERNEL-SET-V1) —
// Scalar-vs-AVX2 parity test for the premultiplied-alpha "SRC_OVER"
// blend (the F5.2 first kernel). Cross-ISA parity is a forward-point
// (forward-point (b) of the new ticket); the first commit certifies
// the `Scalar == AVX2` invariant within `kKernelEpsilon` (1 ULP float32).
// ════════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

#include <chronon3d/simd/cpu_isa.hpp>
#include <chronon3d/simd/detail/scalar_kernels.hpp>
#include <chronon3d/simd/kernel_resolver.hpp>
#include <chronon3d/simd/pixel_kernels.hpp>

#if defined(__AVX2__)
#include <chronon3d/simd/detail/avx2_kernels.hpp> // forward-decl in resolver; includes the impl
#endif

namespace cs = chronon3d::simd;

namespace {

// ── fixture: deterministic pseudo-random RGBA premul values in [0, 1] ──
std::vector<float> make_random_rgba(std::size_t pixel_count, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> out(pixel_count * 4);
    for (auto& v : out) v = dist(rng);
    return out;
}

// ── comparison: 1 ULP bound per `kKernelEpsilon` SSoT ────────────────────
bool within_1ulp(float a, float b) {
    if (a == b) return true;
    const float diff = std::fabs(a - b);
    // Use relative tolerance scaled to the larger magnitude.
    const float mag = std::fmax(std::fabs(a), std::fabs(b));
    return diff <= cs::kKernelEpsilon * mag + cs::kKernelEpsilon;
}

// ── constants: per-N test seeds for cross-N SweepN tests ─────────────
//
// `kSeedSweepAlpha0Identity` drives the seed-offsets in the
// `scalar_blend: identity on alpha=0 SweepN regression` SweepN test
// (per TICKET-SIMD-PRECISION-DRIFT §Forward-points). Extracted from
// inline literal per code-reviewer-minimax-m3 NIT #4.
constexpr std::uint32_t kSeedSweepAlpha0Identity = 0x1D17D17Eu;

} // namespace

TEST_CASE("scalar_blend: identity on alpha=0 (no contribution)") {
    // FIX (TICKET-SIMD-PRECISION-DRIFT macchina-verifica 2026-07-13):
    // Premultiplied-alpha invariant per `include/chronon3d/simd/detail/scalar_kernels.hpp::scalar_blend` doc-comment:
    //   `src.rgb` is ALREADY premultiplied by `src.a` BEFORE the call.
    // Therefore a valid premult color with sa=0 MUST have src.rgb=0.
    // The original fixture {0.1, 0.2, 0.3, 0.0} is an INVALID premult color
    // (rgb not pre-multiplied by alpha=0). Using valid premult, the formula
    // `dst[k] = src[k] + dst[k] * (1 - sa)` → `dst[k] = 0 + dst[k] * 1 = dst[k]`
    // (identity holds).
    std::vector<float> src = {0.0f, 0.0f, 0.0f, 0.0f}; // sa = 0, valid premult (rgb=0)
    std::vector<float> dst = {0.5f, 0.6f, 0.7f, 0.4f};
    const std::vector<float> dst_orig = dst;
    cs::detail::scalar_blend(dst.data(), src.data(), 1);
    // dst untouched (sa=0 → inv=1 → dst = 0 + dst*1 = dst_orig)
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(dst[i] == doctest::Approx(dst_orig[i]));
    }
}

TEST_CASE("scalar_blend: identity on alpha=0 SweepN regression") {
    // Per TICKET-SIMD-PRECISION-DRIFT §Forward-points: regression-prevent SweepN
    // for the alpha=0 identity case over {0, 1, 2, 4, 7, 16, 64, 256, 1024} pixels
    // (mix of powers-of-2 AND 7 odd-size to exercise tail behavior at non-batch sizes).
    for (std::size_t n :
         {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{4},
          std::size_t{7}, std::size_t{16}, std::size_t{64}, std::size_t{256},
          std::size_t{1024}}) {
        // src MUST be 0 for valid premultiplied alpha=0 across N pixels.
        std::vector<float> src(n * 4, 0.0f);
        std::vector<float> dst = make_random_rgba(n, /*seed=*/kSeedSweepAlpha0Identity + static_cast<std::uint32_t>(n));
        const std::vector<float> dst_orig = dst;

        cs::detail::scalar_blend(dst.data(), src.data(), n);

        for (std::size_t i = 0; i < n * 4; ++i) {
            INFO("pixel_count=" << n << " i=" << i);
            CHECK(dst[i] == doctest::Approx(dst_orig[i]));
        }
    }
}

TEST_CASE("scalar_blend: alpha=1 fully replaces dst") {
    std::vector<float> src = {0.1f, 0.2f, 0.3f, 1.0f}; // sa = 1
    std::vector<float> dst = {0.5f, 0.6f, 0.7f, 0.4f};
    cs::detail::scalar_blend(dst.data(), src.data(), 1);
    // dst = src (inv=0; dst * 0 = 0; src + 0 = src)
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(dst[i] == doctest::Approx(src[i]));
    }
    // alpha: sa + dst.a * 0 = sa = 1
    CHECK(dst[3] == doctest::Approx(1.0f));
}

TEST_CASE("scalar_blend: midpoint alpha=0.5 (50/50 blend)") {
    std::vector<float> src = {0.0f, 0.0f, 0.0f, 0.5f}; // sa=0.5
    std::vector<float> dst = {1.0f, 1.0f, 1.0f, 0.0f}; // da=0
    cs::detail::scalar_blend(dst.data(), src.data(), 1);
    // RGB: 0 + 1*0.5 = 0.5; A: 0.5 + 0*0.5 = 0.5
    CHECK(dst[0] == doctest::Approx(0.5f));
    CHECK(dst[1] == doctest::Approx(0.5f));
    CHECK(dst[2] == doctest::Approx(0.5f));
    CHECK(dst[3] == doctest::Approx(0.5f));
}

TEST_CASE("scalar_blend: pixel counts {0, 1, 2, 4, 8, 16, 64, 256, 1024} all correct") {
    for (std::size_t n :
         {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{4},
          std::size_t{8}, std::size_t{16}, std::size_t{64}, std::size_t{256},
          std::size_t{1024}}) {
        const auto src = make_random_rgba(n, /*seed=*/0xC0FFEEu + static_cast<std::uint32_t>(n));
        std::vector<float> dst_scalar = make_random_rgba(n, /*seed=*/0xBADCAFEu + static_cast<std::uint32_t>(n));
        std::vector<float> dst_ref   = dst_scalar; // pure reference (in-place, re-derived)

        // Compute the expected result via the SAME scalar formula, then
        // re-derive independently (two passes) — this certifies the
        // function matches its documented math.
        for (std::size_t i = 0; i < n; ++i) {
            const float sa = src[4 * i + 3];
            const float inv = 1.0f - sa;
            for (std::size_t k = 0; k < 4; ++k) {
                dst_ref[4 * i + k] = src[4 * i + k] + dst_scalar[4 * i + k] * inv;
            }
        }
        cs::detail::scalar_blend(dst_scalar.data(), src.data(), n);
        for (std::size_t i = 0; i < n * 4; ++i) {
            INFO("pixel_count=" << n << " i=" << i);
            CHECK(dst_scalar[i] == doctest::Approx(dst_ref[i]));
        }
    }
}

#if defined(__AVX2__)
// ── AVX2 vs scalar parity (per F5.2 first-kernel) ──────────────────────
TEST_CASE("avx2 vs scalar: parity within 1 ULP across 7 power-of-2 sizes") {
    if (!cs::detect_cpu_capabilities().has_avx2) {
        WARN("CPU lacks AVX2 — skipping avx2 parity test on this host");
        return;
    }
    for (std::size_t n : {std::size_t{1}, std::size_t{2}, std::size_t{4},
                          std::size_t{16}, std::size_t{64}, std::size_t{256},
                          std::size_t{4096}}) {
        const auto src = make_random_rgba(n, /*seed=*/0xA1A1A1u + static_cast<std::uint32_t>(n));
        std::vector<float> dst_scalar = make_random_rgba(n, /*seed=*/0xB2B2B2u + static_cast<std::uint32_t>(n));
        std::vector<float> dst_avx2   = dst_scalar;

        cs::detail::scalar_blend(dst_scalar.data(), src.data(), n);
        cs::detail::avx2_composite_normal_premul(dst_avx2.data(), src.data(), n);

        for (std::size_t i = 0; i < n * 4; ++i) {
            INFO("pixel_count=" << n << " i=" << i);
            CHECK(within_1ulp(dst_scalar[i], dst_avx2[i]));
        }
    }
}

TEST_CASE("avx2 vs scalar: 1-pixel tail (odd sizes) parity") {
    if (!cs::detect_cpu_capabilities().has_avx2) {
        WARN("CPU lacks AVX2 — skipping avx2 parity test on this host");
        return;
    }
    for (std::size_t n : {std::size_t{1}, std::size_t{3}, std::size_t{5},
                          std::size_t{7}, std::size_t{15}, std::size_t{17}}) {
        const auto src = make_random_rgba(n, /*seed=*/0xC3C3C3u + static_cast<std::uint32_t>(n));
        std::vector<float> dst_scalar = make_random_rgba(n, /*seed=*/0xD4D4D4u + static_cast<std::uint32_t>(n));
        std::vector<float> dst_avx2   = dst_scalar;

        cs::detail::scalar_blend(dst_scalar.data(), src.data(), n);
        cs::detail::avx2_composite_normal_premul(dst_avx2.data(), src.data(), n);

        for (std::size_t i = 0; i < n * 4; ++i) {
            INFO("pixel_count=" << n << " i=" << i);
            CHECK(within_1ulp(dst_scalar[i], dst_avx2[i]));
        }
    }
}
#endif // __AVX2__

TEST_CASE("resolver: kScalarSet always available, kAvx2Set gated on has_avx2") {
    // kScalarSet is the ALWAYS-AVAILABLE reference (per F5.1).
    const auto& scalar_set = cs::kScalarSet;
    CHECK(scalar_set.blend.apply != nullptr);
    CHECK(scalar_set.blur.apply != nullptr);
    CHECK(scalar_set.glow.apply != nullptr);
    CHECK(scalar_set.resample.apply != nullptr);
    CHECK(scalar_set.color_matrix.apply != nullptr);

    // resolve_pixel_kernels routes per CpuCapabilities (F5.2 first kernel).
    cs::CpuCapabilities caps_scalar{cs::CpuIsa::Scalar, false, false, false, false};
    const auto& set_scalar = cs::resolve_pixel_kernels(caps_scalar);
    CHECK(set_scalar.blend.apply == scalar_set.blend.apply);

#if defined(__AVX2__)
    if (cs::detect_cpu_capabilities().has_avx2) {
        cs::CpuCapabilities caps_avx2{cs::CpuIsa::AVX2, true, true, false, false};
        const auto& set_avx2 = cs::resolve_pixel_kernels(caps_avx2);
        // For AVX2 hosts, the resolver MUST return a set with the AVX2
        // blend fn (if first-kernel landed; otherwise scalar fallback).
        // The forward-point (c) of TICKET-SIMD-VECTORIZE-KERNEL-SET-V1
        // registers future per-ISA sets; for THIS commit only the blend
        // slot MAY differ from kScalarSet.
        CHECK(set_avx2.blend.apply != nullptr);
        // The scalar fallback for non-AVX2 slots MUST be populated.
        CHECK(set_avx2.blur.apply  == scalar_set.blur.apply);
        CHECK(set_avx2.glow.apply  == scalar_set.glow.apply);
        CHECK(set_avx2.resample.apply  == scalar_set.resample.apply);
        CHECK(set_avx2.color_matrix.apply == scalar_set.color_matrix.apply);
    }
#endif
}
