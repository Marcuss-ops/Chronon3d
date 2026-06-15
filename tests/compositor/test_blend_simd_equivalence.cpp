// =============================================================================
// test_blend_simd_equivalence.cpp — Scalar vs SIMD pixel-level equivalence
//
// For each blend mode:
//   1. 50,000 random premultiplied pairs at single-pixel granularity
//   2. Various pixel_count sizes (1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33)
//   3. All pixel values checked for finiteness
//
// On failure, prints:
//   seed, index, mode, source, destination, expected, actual
// =============================================================================

#include <doctest/doctest.h>
#include <chronon3d/compositor/blend_math.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <tests/helpers/check_helpers.hpp>

#include <random>
#include <cmath>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::blend_math;
using namespace chronon3d::test;

namespace {

// ── Shared RNG with spec seed ────────────────────────────────────────────
static constexpr uint32_t kSpecSeed = 0xC0FFEE;

std::mt19937& rng() {
    static std::mt19937 gen(kSpecSeed);
    return gen;
}

Color random_premul(float alpha_min = 0.0f, float alpha_max = 1.0f,
                     float rgb_min = -0.5f, float rgb_max = 4.0f) {
    std::uniform_real_distribution<float> d_rgb(rgb_min, rgb_max);
    std::uniform_real_distribution<float> d_a(alpha_min, alpha_max);
    float a = d_a(rng());
    return {d_rgb(rng()) * a, d_rgb(rng()) * a, d_rgb(rng()) * a, a};
}

// ── Dispatch ─────────────────────────────────────────────────────────────
void apply_simd(Color& dst, const Color& src, BlendMode mode) {
    switch (mode) {
        case BlendMode::Darken:      simd::composite_darken_premul(&dst, &src, 1); break;
        case BlendMode::Lighten:     simd::composite_lighten_premul(&dst, &src, 1); break;
        case BlendMode::Difference:  simd::composite_difference_premul(&dst, &src, 1); break;
        case BlendMode::Exclusion:   simd::composite_exclusion_premul(&dst, &src, 1); break;
        case BlendMode::SoftLight:   simd::composite_soft_light_premul(&dst, &src, 1); break;
        case BlendMode::HardLight:   simd::composite_hard_light_premul(&dst, &src, 1); break;
        case BlendMode::ColorDodge:  simd::composite_color_dodge_premul(&dst, &src, 1); break;
        case BlendMode::ColorBurn:   simd::composite_color_burn_premul(&dst, &src, 1); break;
        default: FAIL("Unsupported mode: " << static_cast<int>(mode));
    }
}

// ── Core test driver ────────────────────────────────────────────────────
void test_simd_equivalence(BlendMode mode, int num_samples = 50000) {
    rng().seed(kSpecSeed);  // Reset for reproducibility.

    for (int i = 0; i < num_samples; ++i) {
        Color src = random_premul();
        Color dst = random_premul();
        Color dst_copy = dst;

        // Scalar reference (truth).
        Color expected = blend_reference_premul(src, dst_copy, mode);

        // SIMD path.
        Color simd_result = dst;
        apply_simd(simd_result, src, mode);

        // Compare.
        if (std::abs(simd_result.r - expected.r) > kEpsilonSIMD ||
            std::abs(simd_result.g - expected.g) > kEpsilonSIMD ||
            std::abs(simd_result.b - expected.b) > kEpsilonSIMD ||
            std::abs(simd_result.a - expected.a) > kEpsilonSIMD) {
            FAIL("Mismatch at sample " << i
                 << " seed=" << kSpecSeed
                 << " mode=" << static_cast<int>(mode)
                 << " src=(" << src.r << "," << src.g << "," << src.b << "," << src.a << ")"
                 << " dst=(" << dst.r << "," << dst.g << "," << dst.b << "," << dst.a << ")"
                 << " expected=(" << expected.r << "," << expected.g << "," << expected.b << "," << expected.a << ")"
                 << " simd=(" << simd_result.r << "," << simd_result.g << "," << simd_result.b << "," << simd_result.a << ")");
        }
    }
}

// ── Multi-pixel batch test driver ────────────────────────────────────────
// Properly calls the actual SIMD kernel functions with full arrays of each
// size, exercising AVX2 2-pixel paths, FixedTag remainder paths, etc.
using BatchFunc = void (*)(Color* dst, const Color* src, int count);

BatchFunc get_batch_func(BlendMode mode) {
    switch (mode) {
        case BlendMode::Darken:      return simd::composite_darken_premul;
        case BlendMode::Lighten:     return simd::composite_lighten_premul;
        case BlendMode::Difference:  return simd::composite_difference_premul;
        case BlendMode::Exclusion:   return simd::composite_exclusion_premul;
        case BlendMode::SoftLight:   return simd::composite_soft_light_premul;
        case BlendMode::HardLight:   return simd::composite_hard_light_premul;
        case BlendMode::ColorDodge:  return simd::composite_color_dodge_premul;
        case BlendMode::ColorBurn:   return simd::composite_color_burn_premul;
        default: FAIL("Unsupported mode"); return nullptr;
    }
}

void test_simd_batch_sizes(BlendMode mode) {
    rng().seed(kSpecSeed);
    auto batch_func = get_batch_func(mode);

    // Test sizes that exercise SIMD remainder paths and vectorized loops.
    std::vector<int> sizes = {1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 1921};

    for (int n : sizes) {
        std::vector<Color> src(n);
        std::vector<Color> dst(n);
        std::vector<Color> expected(n);

        for (int i = 0; i < n; ++i) {
            src[i] = random_premul();
            dst[i] = random_premul();
            expected[i] = blend_reference_premul(src[i], dst[i], mode);
        }

        // Apply SIMD kernel ONCE on the whole array — this is the key fix.
        std::vector<Color> simd_result = dst;
        batch_func(simd_result.data(), src.data(), n);

        for (int i = 0; i < n; ++i) {
            check_color_close(simd_result[i], expected[i], kEpsilonSIMD);
        }
    }
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// 50,000 random pairs per mode
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend_simd: Darken — 50000 random scalar vs SIMD") {
    test_simd_equivalence(BlendMode::Darken, 50000);
}

TEST_CASE("blend_simd: Lighten — 50000 random scalar vs SIMD") {
    test_simd_equivalence(BlendMode::Lighten, 50000);
}

TEST_CASE("blend_simd: Difference — 50000 random scalar vs SIMD") {
    test_simd_equivalence(BlendMode::Difference, 50000);
}

TEST_CASE("blend_simd: Exclusion — 50000 random scalar vs SIMD") {
    test_simd_equivalence(BlendMode::Exclusion, 50000);
}

TEST_CASE("blend_simd: SoftLight — 50000 random scalar vs SIMD") {
    test_simd_equivalence(BlendMode::SoftLight, 50000);
}

TEST_CASE("blend_simd: HardLight — 50000 random scalar vs SIMD") {
    test_simd_equivalence(BlendMode::HardLight, 50000);
}

TEST_CASE("blend_simd: ColorDodge — 50000 random scalar vs SIMD") {
    test_simd_equivalence(BlendMode::ColorDodge, 50000);
}

TEST_CASE("blend_simd: ColorBurn — 50000 random scalar vs SIMD") {
    test_simd_equivalence(BlendMode::ColorBurn, 50000);
}

// ═════════════════════════════════════════════════════════════════════════════
// Multi-pixel batch tests (various sizes for SIMD remainder paths)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend_simd: Darken — batch sizes") {
    test_simd_batch_sizes(BlendMode::Darken);
}
TEST_CASE("blend_simd: Lighten — batch sizes") {
    test_simd_batch_sizes(BlendMode::Lighten);
}
TEST_CASE("blend_simd: Difference — batch sizes") {
    test_simd_batch_sizes(BlendMode::Difference);
}
TEST_CASE("blend_simd: Exclusion — batch sizes") {
    test_simd_batch_sizes(BlendMode::Exclusion);
}
TEST_CASE("blend_simd: SoftLight — batch sizes") {
    test_simd_batch_sizes(BlendMode::SoftLight);
}
TEST_CASE("blend_simd: HardLight — batch sizes") {
    test_simd_batch_sizes(BlendMode::HardLight);
}
TEST_CASE("blend_simd: ColorDodge — batch sizes") {
    test_simd_batch_sizes(BlendMode::ColorDodge);
}
TEST_CASE("blend_simd: ColorBurn — batch sizes") {
    test_simd_batch_sizes(BlendMode::ColorBurn);
}

// ═════════════════════════════════════════════════════════════════════════════
// Regression: compositor::blend (existing switch) matches blend_reference_premul
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend_simd: Normal compositor::blend matches blend_reference_premul") {
    rng().seed(kSpecSeed);
    for (int i = 0; i < 10000; ++i) {
        Color src = random_premul();
        Color dst = random_premul();

        // compositor::blend(Normal) → blend_normal() expects straight-alpha source.
        // Convert from premultiplied to straight before passing to compositor::blend.
        Color straight_src{
            src.a > 1e-8f ? src.r / src.a : 0.0f,
            src.a > 1e-8f ? src.g / src.a : 0.0f,
            src.a > 1e-8f ? src.b / src.a : 0.0f,
            src.a
        };
        Color expected = compositor::blend(straight_src, dst, BlendMode::Normal);

        // Via blend_reference_premul (canonical premultiplied formula from blend_math.hpp).
        Color ref = blend_reference_premul(src, dst, BlendMode::Normal);

        check_color_close(expected, ref, kEpsilonScalar);
    }
}
