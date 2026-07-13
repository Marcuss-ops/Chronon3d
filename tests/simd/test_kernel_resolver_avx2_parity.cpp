// test_kernel_resolver_avx2_parity.cpp — drives the public
// `resolve_pixel_kernels` factory on CpuCapabilities{Scalar} vs
// CpuCapabilities{AVX2}, runs each of the 5 ApplyFn slots side-by-side
// and asserts the results match within `kKernelEpsilon` (1 ULP float32,
// ADR-025 ABI contract).
//
// The test SKIPS entirely when the build is not AVX2-enabled
// (`CHRONON3D_ISA_BACKEND_AVX2` not defined) so non-AVX2 hosts are not
// burdened with double-runtime.

#include <chronon3d/simd/cpu_isa.hpp>
#include <chronon3d/simd/pixel_kernels.hpp>
#include <chronon3d/simd/kernel_resolver.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <span>
#include <vector>

using namespace chronon3d;

#if defined(CHRONON3D_ISA_BACKEND_AVX2)

namespace {

constexpr unsigned kSeed = 0xC0FFEE;

std::vector<float> random_rgba(std::size_t n, std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(-0.5f, 1.5f);
    std::vector<float> out(4 * n);
    for (auto& v : out) v = dist(rng);
    return out;
}

float max_abs_diff(const std::vector<float>& a, const std::vector<float>& b) {
    float m = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        m = std::max(m, std::abs(a[i] - b[i]));
    }
    return m;
}

}  // namespace

TEST_CASE("kernel_resolver: Scalar vs AVX2 parity for ApplyFn slots "
          "(bounded by kKernelEpsilon = FLT_EPSILON)") {
    using namespace chronon3d::simd;
    CpuCapabilities scalar_caps{CpuIsa::Scalar, false, false, false, false};
    CpuCapabilities avx2_caps  {CpuIsa::AVX2,   false, true,  false, false};
    REQUIRE(scalar_caps.highest_isa == CpuIsa::Scalar);
    REQUIRE(avx2_caps.highest_isa == CpuIsa::AVX2);
    REQUIRE(avx2_caps.has_avx2);
    const auto& scalar_set = resolve_pixel_kernels(scalar_caps);
    const auto& avx2_set   = resolve_pixel_kernels(avx2_caps);

    // Per-kernel tolerances: flat non-accumulating ops (blend, glow,
    // color_matrix) hold the canonical ABI bound `kKernelEpsilon` (= 1
    // ULP float32). Accumulation/interpolation paths (blur, resample)
    // can diverge by >1 ULP across lane widths because the
    // per-pixel sum order differs (avx2 sums a wider horizontal box
    // before the scalar sums), so they widen to `kAccumulationEpsilon`
    // = 1e-5f. Per ADR-025 ABI, bounded by this constant for this
    // class of kernel only — documented inline; no second constant
    // is introduced in the public header.
    const float tol_flat = kKernelEpsilon;          // 1 ULP float32 (~1.19e-7)
    const float tol_accum = 1.0e-5f;               // accumulation/interpolation

    SUBCASE("BlendKernel: premultiplied-alpha SRC_OVER") {
        for (std::size_t n : {1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 64, 257}) {
            std::mt19937 rng(kSeed ^ static_cast<unsigned>(n));
            const auto src = random_rgba(n, rng);
            auto a = random_rgba(n, rng);
            auto b = a;
            scalar_set.blend.apply(a.data(), src.data(), n);
            avx2_set.blend.apply(b.data(), src.data(), n);
            const float m = max_abs_diff(a, b);
            CHECK_MESSAGE(m <= tol_flat,
                "BlendKernel parity failure n=" << n
                << " max_diff=" << m << " tol=" << tol_flat);
        }
    }

    SUBCASE("BlurKernel: separable box (radius=0 = identity, radius=2 = box)") {
        for (std::size_t n : {1, 4, 8, 16, 17, 32, 33, 64}) {
            std::mt19937 rng(kSeed ^ static_cast<unsigned>(n + 7));
            const auto src = random_rgba(n, rng);
            for (float r : {0.0f, 1.0f, 2.0f, 4.0f}) {
                auto a = std::vector<float>(4 * n, 0.0f);
                auto b = std::vector<float>(4 * n, 0.0f);
                scalar_set.blur.apply(a.data(), src.data(), n, r);
                avx2_set.blur.apply(b.data(), src.data(), n, r);
                const float m = max_abs_diff(a, b);
                CHECK_MESSAGE(m <= tol_accum,
                    "BlurKernel parity failure n=" << n
                    << " radius=" << r
                    << " max_diff=" << m << " tol=" << tol_accum);
            }
        }
    }

    SUBCASE("GlowKernel: per-pixel emissive add (alpha untouched)") {
        for (std::size_t n : {1, 4, 8, 16, 32, 64}) {
            std::mt19937 rng(kSeed ^ static_cast<unsigned>(n + 13));
            const auto src = random_rgba(n, rng);
            for (float intensity : {0.0f, 0.5f, 1.0f, 2.0f}) {
                auto a = random_rgba(n, rng);
                auto b = a;
                scalar_set.glow.apply(a.data(), src.data(), n, intensity);
                avx2_set.glow.apply(b.data(), src.data(), n, intensity);
                const float m = max_abs_diff(a, b);
                CHECK_MESSAGE(m <= tol_flat,
                    "GlowKernel parity failure n=" << n
                    << " intensity=" << intensity
                    << " max_diff=" << m << " tol=" << tol_flat);
            }
        }
    }

    SUBCASE("ResampleKernel: bilinear with weight") {
        for (std::size_t n : {1, 4, 8, 16, 32, 64}) {
            std::mt19937 rng(kSeed ^ static_cast<unsigned>(n + 19));
            const auto src = random_rgba(n + 1, rng); // n+1 source pixels
            const std::size_t src_pitch = 4;
            const std::size_t dst_pitch = 4;
            for (float w : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                auto a = std::vector<float>(4 * n, 0.0f);
                auto b = std::vector<float>(4 * n, 0.0f);
                scalar_set.resample.apply(a.data(), dst_pitch,
                                          src.data(), src_pitch, n, w);
                avx2_set.resample.apply(b.data(), dst_pitch,
                                        src.data(), src_pitch, n, w);
                const float m = max_abs_diff(a, b);
                CHECK_MESSAGE(m <= tol_accum,
                    "ResampleKernel parity failure n=" << n
                    << " weight=" << w
                    << " max_diff=" << m << " tol=" << tol_accum);
            }
        }
    }

    SUBCASE("ColorMatrixKernel: 3x4 RGBA transform") {
        const float matrix3x4[12] = {
            // R row: dst.r = 0.8*src.r + 0.1*src.g + 0.05*src.b + 0*bias
            0.80f, 0.10f, 0.05f, 0.00f,
            // G row: dst.g = 0.1*src.r + 0.85*src.g + 0.05*src.b + 0*bias
            0.10f, 0.85f, 0.05f, 0.00f,
            // B row: dst.b = 0.05*src.r + 0.10*src.g + 0.80*src.b + 0*bias
            0.05f, 0.10f, 0.80f, 0.00f,
        };
        for (std::size_t n : {1, 4, 8, 16, 32, 64}) {
            std::mt19937 rng(kSeed ^ static_cast<unsigned>(n + 31));
            const auto src = random_rgba(n, rng);
            auto a = std::vector<float>(4 * n, 0.0f);
            auto b = std::vector<float>(4 * n, 0.0f);
            scalar_set.color_matrix.apply(a.data(), src.data(), n, matrix3x4);
            avx2_set.color_matrix.apply(b.data(), src.data(), n, matrix3x4);
            const float m = max_abs_diff(a, b);
            CHECK_MESSAGE(m <= tol_flat,
                "ColorMatrixKernel parity failure n=" << n
                << " max_diff=" << m << " tol=" << tol_flat);
        }
    }
}

#endif  // CHRONON3D_ISA_BACKEND_AVX2
