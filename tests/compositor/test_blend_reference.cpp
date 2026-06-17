// =============================================================================
// test_blend_reference.cpp — Scalar blend reference tests with exact numbers
//
// All numerical values are taken from the B2/B3 spec and verified by hand.
//
// Common inputs (from spec):
//   Source straight:  Cs = (0.8, 0.2, 0.4),  As = 0.5
//   Source premul:    cs = (0.4, 0.1, 0.2, 0.5)
//   Backdrop straight: Cb = (0.2, 0.6, 0.5), Ab = 0.75
//   Backdrop premul:   cb = (0.15, 0.45, 0.375, 0.75)
//   Ao = As + Ab - As*Ab = 0.5 + 0.75 - 0.5*0.75 = 0.875
// =============================================================================

#include <doctest/doctest.h>
#include <chronon3d/compositor/blend_math.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <tests/helpers/check_helpers.hpp>

#include <random>
#include <cmath>
using namespace chronon3d;

using namespace chronon3d::blend_math;
using namespace chronon3d::test;

namespace {

// ── Common test colors ───────────────────────────────────────────────────
// Source straight (0.8, 0.2, 0.4), As=0.5
// Premul: (0.4, 0.1, 0.2, 0.5)
constexpr Color kSrcPremul{0.4f, 0.1f, 0.2f, 0.5f};

// Backdrop straight (0.2, 0.6, 0.5), Ab=0.75
// Premul: (0.15, 0.45, 0.375, 0.75)
constexpr Color kDstPremul{0.15f, 0.45f, 0.375f, 0.75f};

// Common alpha output: Ao = As + Ab - As*Ab = 0.875
constexpr float kAo = 0.875f;

// Deterministic RNG for randomized tests.
std::mt19937& rng() {
    static std::mt19937 gen(0xC0FFEE);
    return gen;
}

Color make_random_premultiplied() {
    std::uniform_real_distribution<float> d(0.0f, 1.0f);
    float a = d(rng());
    return {d(rng()) * a, d(rng()) * a, d(rng()) * a, a};
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Premultiplied alpha round-trip (shared across all effects, from spec)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("premultiply and unpremultiply roundtrip") {
    test_premultiply_roundtrip();
}

// ═════════════════════════════════════════════════════════════════════════════
// B2: Exact reference values (spec §B2)
//
// These test the canonical blend_reference_premul() function directly
// with the known Cs/Cb values from the spec.
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend_ref: Normal — exact spec values") {
    Color r = blend_reference_premul(kSrcPremul, kDstPremul, BlendMode::Normal);
    // Co = cs + cb*(1-As) = (0.4, 0.1, 0.2) + (0.15, 0.45, 0.375)*(1-0.5)
    //    = (0.4+0.075, 0.1+0.225, 0.2+0.1875, 0.875)
    //    = (0.475, 0.325, 0.3875, 0.875)
    check_color_close(r, {0.475f, 0.325f, 0.3875f, kAo});
}

TEST_CASE("blend_ref: Multiply — exact spec values") {
    Color r = blend_reference_premul(kSrcPremul, kDstPremul, BlendMode::Multiply);
    // Blend B = (Cs*Cb) = (0.8*0.2, 0.2*0.6, 0.4*0.5) = (0.16, 0.12, 0.20)
    // Co = cb*(1-As) + cs*(1-Ab) + B*(As*Ab)
    // = (0.15*0.5 + 0.4*0.25 + 0.16*0.375,
    //    0.45*0.5 + 0.1*0.25 + 0.12*0.375,
    //    0.375*0.5 + 0.2*0.25 + 0.20*0.375,
    //    0.875)
    // = (0.075 + 0.1 + 0.06,  0.225 + 0.025 + 0.045,  0.1875 + 0.05 + 0.075)
    // = (0.235, 0.295, 0.3125, 0.875)
    check_color_close(r, {0.235f, 0.295f, 0.3125f, kAo});
}

TEST_CASE("blend_ref: Screen — exact spec values") {
    Color r = blend_reference_premul(kSrcPremul, kDstPremul, BlendMode::Screen);
    // Blend B = Cs + Cb - Cs*Cb
    // R: 0.8 + 0.2 - 0.16 = 0.84
    // G: 0.2 + 0.6 - 0.12 = 0.68
    // B: 0.4 + 0.5 - 0.20 = 0.70
    // Co = cb*(1-As) + cs*(1-Ab) + B*(As*Ab)
    // = (0.15*0.5 + 0.4*0.25 + 0.84*0.375,  ...)
    // = (0.075 + 0.1 + 0.315,  0.225 + 0.025 + 0.255,  0.1875 + 0.05 + 0.2625)
    // = (0.49, 0.505, 0.50, 0.875)
    check_color_close(r, {0.49f, 0.505f, 0.50f, kAo});
}

TEST_CASE("blend_ref: Overlay — exact spec values") {
    Color r = blend_reference_premul(kSrcPremul, kDstPremul, BlendMode::Overlay);
    // Blend: if Cb < 0.5 → 2*Cs*Cb, else → 1 - 2*(1-Cs)*(1-Cb)
    // R: Cb=0.2 < 0.5 → 2*0.8*0.2 = 0.32
    // G: Cb=0.6 ≥ 0.5 → 1 - 2*(1-0.2)*(1-0.6) = 1 - 2*0.8*0.4 = 1 - 0.64 = 0.36
    // B: Cb=0.5 → either branch: 2*0.4*0.5 = 0.40  or  1 - 2*(1-0.4)*(1-0.5) = 1 - 2*0.6*0.5 = 0.40
    // Co = (0.15*0.5 + 0.4*0.25 + 0.32*0.375,
    //       0.45*0.5 + 0.1*0.25 + 0.36*0.375,
    //       0.375*0.5 + 0.2*0.25 + 0.40*0.375,
    //       0.875)
    // =  (0.075+0.1+0.12,  0.225+0.025+0.135,  0.1875+0.05+0.15)
    // = (0.295, 0.385, 0.3875, 0.875)
    check_color_close(r, {0.295f, 0.385f, 0.3875f, kAo});
}

TEST_CASE("blend_ref: Darken — exact spec values") {
    Color r = blend_reference_premul(kSrcPremul, kDstPremul, BlendMode::Darken);
    // Blend B = min(Cs, Cb) = (0.2, 0.2, 0.4)
    // Co = (0.075+0.1+0.2*0.375,  0.225+0.025+0.2*0.375,  0.1875+0.05+0.4*0.375)
    // = (0.075+0.1+0.075,  0.225+0.025+0.075,  0.1875+0.05+0.15)
    // = (0.25, 0.325, 0.3875, 0.875)
    check_color_close(r, {0.25f, 0.325f, 0.3875f, kAo});
}

TEST_CASE("blend_ref: Lighten — exact spec values") {
    Color r = blend_reference_premul(kSrcPremul, kDstPremul, BlendMode::Lighten);
    // Blend B = max(Cs, Cb) = (0.8, 0.6, 0.5)
    // Co = (0.075+0.1+0.8*0.375,  0.225+0.025+0.6*0.375,  0.1875+0.05+0.5*0.375)
    // = (0.075+0.1+0.3,  0.225+0.025+0.225,  0.1875+0.05+0.1875)
    // = (0.475, 0.475, 0.425, 0.875)
    check_color_close(r, {0.475f, 0.475f, 0.425f, kAo});
}

TEST_CASE("blend_ref: Difference — exact spec values") {
    Color r = blend_reference_premul(kSrcPremul, kDstPremul, BlendMode::Difference);
    // Blend B = |Cs - Cb| = (0.6, 0.4, 0.1)
    // Co = (0.075+0.1+0.6*0.375,  0.225+0.025+0.4*0.375,  0.1875+0.05+0.1*0.375)
    // = (0.075+0.1+0.225,  0.225+0.025+0.15,  0.1875+0.05+0.0375)
    // = (0.4, 0.4, 0.275, 0.875)
    check_color_close(r, {0.4f, 0.4f, 0.275f, kAo});
}

TEST_CASE("blend_ref: Exclusion — exact spec values") {
    Color r = blend_reference_premul(kSrcPremul, kDstPremul, BlendMode::Exclusion);
    // Blend B = Cs + Cb - 2*Cs*Cb
    // R: 0.8+0.2-2*0.16 = 0.68
    // G: 0.2+0.6-2*0.12 = 0.56
    // B: 0.4+0.5-2*0.20 = 0.50
    // Co = (0.075+0.1+0.68*0.375,  0.225+0.025+0.56*0.375,  0.1875+0.05+0.5*0.375)
    // = (0.075+0.1+0.255,  0.225+0.025+0.21,  0.1875+0.05+0.1875)
    // = (0.43, 0.46, 0.425, 0.875)
    check_color_close(r, {0.43f, 0.46f, 0.425f, kAo});
}

TEST_CASE("blend_ref: Add — HDR, no clamp") {
    // Add == source + backdrop (HDR, not clamped)
    Color r = compositor::blend(kSrcPremul, kDstPremul, BlendMode::Add);
    // (0.4+0.15, 0.1+0.45, 0.2+0.375, 0.5+0.75) = (0.55, 0.55, 0.575, 1.25)
    check_color_close(r, {0.55f, 0.55f, 0.575f, 1.25f});
}

// ═════════════════════════════════════════════════════════════════════════════
// B3: Advanced blend exact values
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend_ref_b3: HardLight — exact spec values") {
    // Cb = (0.2, 0.6, 0.5), Cs = (0.8, 0.2, 0.4)
    // Direct straight blend:
    // R: Cs=0.8 > 0.5 → 1 - 2*(1-0.2)*(1-0.8) = 1 - 2*0.8*0.2 = 1 - 0.32 = 0.68
    // G: Cs=0.2 ≤ 0.5 → 2*0.6*0.2 = 0.24
    // B: Cs=0.4 ≤ 0.5 → 2*0.5*0.4 = 0.40
    // Then apply canonical premultiplied formula.
    check_close(blend_hard_light(0.2f, 0.8f), 0.68f);
    check_close(blend_hard_light(0.6f, 0.2f), 0.24f);
    check_close(blend_hard_light(0.5f, 0.4f), 0.40f);
}

TEST_CASE("blend_ref_b3: SoftLight — exact spec values") {
    // Cb = (0.2, 0.6, 0.5), Cs = (0.8, 0.2, 0.4)
    // R: Cb=0.2, Cs=0.8 → cs>0.5 → cb + (2*cs-1)*(d(cb)-cb)
    //    d(0.2): 0.2<=0.25 → ((16*0.2-12)*0.2+4)*0.2 = ((3.2-12)*0.2+4)*0.2
    //          = (-8.8*0.2+4)*0.2 = (-1.76+4)*0.2 = 2.24*0.2 = 0.448
    //    = 0.2 + (2*0.8-1)*(0.448-0.2) = 0.2 + 0.6*0.248 = 0.2 + 0.1488 = 0.3488
    // G: Cb=0.6, Cs=0.2 → cs≤0.5 → cb - (1-2*cs)*cb*(1-cb)
    //    = 0.6 - (1-0.4)*0.6*0.4 = 0.6 - 0.6*0.6*0.4 = 0.6 - 0.144 = 0.456
    // B: Cb=0.5, Cs=0.4 → cs≤0.5 → 0.5 - (1-0.8)*0.5*0.5 = 0.5 - 0.2*0.5*0.5 = 0.5 - 0.05 = 0.45
    check_close(blend_soft_light(0.2f, 0.8f), 0.3488f, kEpsilonScalar);
    check_close(blend_soft_light(0.6f, 0.2f), 0.456f, kEpsilonScalar);
    check_close(blend_soft_light(0.5f, 0.4f), 0.45f, kEpsilonScalar);
}

TEST_CASE("blend_ref_b3: ColorDodge — exact spec values") {
    // Cb = (0.8, 0.4, 0.2), Cs = (0.5, 0.8, 0.4)
    // R: min(1, 0.8/max(1-0.5, eps)) = min(1, 0.8/0.5) = min(1, 1.6) = 1
    // G: min(1, 0.4/max(1-0.8, eps)) = min(1, 0.4/0.2) = min(1, 2) = 1
    // B: min(1, 0.2/max(1-0.4, eps)) = min(1, 0.2/0.6) = min(1, 0.3333) = 0.3333
    check_close(blend_color_dodge(0.8f, 0.5f), 1.0f);
    check_close(blend_color_dodge(0.4f, 0.8f), 1.0f);
    check_close(blend_color_dodge(0.2f, 0.4f), 0.3333333f, kEpsilonScalar);
}

TEST_CASE("blend_ref_b3: ColorBurn — exact spec values") {
    // Cb = (0.8, 0.4, 0.2), Cs = (0.5, 0.8, 0.4)
    // R: 1 - min(1, (1-0.8)/max(0.5, eps)) = 1 - min(1, 0.2/0.5) = 1 - min(1, 0.4) = 0.6
    // G: 1 - min(1, (1-0.4)/max(0.8, eps)) = 1 - min(1, 0.6/0.8) = 1 - min(1, 0.75) = 0.25
    // B: 1 - min(1, (1-0.2)/max(0.4, eps)) = 1 - min(1, 0.8/0.4) = 1 - min(1, 2) = 0
    check_close(blend_color_burn(0.8f, 0.5f), 0.6f);
    check_close(blend_color_burn(0.4f, 0.8f), 0.25f);
    check_close(blend_color_burn(0.2f, 0.4f), 0.0f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Universal: Alpha invariants — all non-Add modes use same alpha formula
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend: alpha output is Ao = As + Ab - As*Ab for all non-Add modes") {
    for (int m = 0; m <= static_cast<int>(BlendMode::ColorBurn); ++m) {
        auto mode = static_cast<BlendMode>(m);
        if (mode == BlendMode::Add) continue;
        Color r = blend_reference_premul(kSrcPremul, kDstPremul, mode);
        check_close(r.a, kAo);
    }
}

TEST_CASE("blend: source alpha 0 → destination unchanged") {
    Color transparent_src{0.5f, 0.3f, 0.7f, 0.0f};
    for (int m = 0; m <= static_cast<int>(BlendMode::ColorBurn); ++m) {
        auto mode = static_cast<BlendMode>(m);
        if (mode == BlendMode::Add) continue;
        Color r = blend_reference_premul(transparent_src, kDstPremul, mode);
        check_color_close(r, kDstPremul);
    }
}

TEST_CASE("blend: destination alpha 0 → source unchanged") {
    // Use a valid premultiplied transparent color (alpha=0, RGB=0).
    Color transparent_dst{0.0f, 0.0f, 0.0f, 0.0f};
    for (int m = 0; m <= static_cast<int>(BlendMode::ColorBurn); ++m) {
        auto mode = static_cast<BlendMode>(m);
        if (mode == BlendMode::Add) continue;
        Color r = blend_reference_premul(kSrcPremul, transparent_dst, mode);
        check_color_close(r, kSrcPremul);
    }
}

TEST_CASE("blend: both alpha 0 → transparent") {
    for (int m = 0; m <= static_cast<int>(BlendMode::ColorBurn); ++m) {
        auto mode = static_cast<BlendMode>(m);
        if (mode == BlendMode::Add) continue;
        Color r = blend_reference_premul(
            Color{0.0f, 0.0f, 0.0f, 0.0f},
            Color{0.0f, 0.0f, 0.0f, 0.0f},
            mode);
        check_color_close(r, Color::transparent());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Universal: Commutativity
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend: commutative modes") {
    // Must be commutative: Multiply, Screen, Darken, Lighten, Difference, Exclusion
    const BlendMode commutative[] = {
        BlendMode::Multiply, BlendMode::Screen, BlendMode::Darken,
        BlendMode::Lighten, BlendMode::Difference, BlendMode::Exclusion
    };
    for (auto mode : commutative) {
        for (int i = 0; i < 20; ++i) {
            Color a = make_random_premultiplied();
            Color b = make_random_premultiplied();
            Color ab = blend_reference_premul(a, b, mode);
            Color ba = blend_reference_premul(b, a, mode);
            check_color_close(ab, ba, kEpsilonScalar);
        }
    }
}

TEST_CASE("blend: non-commutative modes produce different results for swapped inputs") {
    // Overlay, HardLight, SoftLight, ColorDodge, ColorBurn are not commutative.
    const BlendMode non_commutative[] = {
        BlendMode::Overlay, BlendMode::HardLight, BlendMode::SoftLight,
        BlendMode::ColorDodge, BlendMode::ColorBurn
    };
    for (auto mode : non_commutative) {
        bool found_non_commutative = false;
        for (int trial = 0; trial < 20; ++trial) {
            Color a = make_random_premultiplied();
            Color b = make_random_premultiplied();
            Color ab = blend_reference_premul(a, b, mode);
            Color ba = blend_reference_premul(b, a, mode);
            bool same = (std::abs(ab.r - ba.r) < 1e-6f) &&
                        (std::abs(ab.g - ba.g) < 1e-6f) &&
                        (std::abs(ab.b - ba.b) < 1e-6f) &&
                        (std::abs(ab.a - ba.a) < 1e-6f);
            if (!same) {
                found_non_commutative = true;
                break;
            }
        }
        CHECK_MESSAGE(found_non_commutative,
                      "Mode " << static_cast<int>(mode) << " appears commutative over 20 random trials");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Universal: Identity properties
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend: identity properties") {
    Color white{1.0f, 1.0f, 1.0f, 1.0f};
    Color black{0.0f, 0.0f, 0.0f, 1.0f};
    Color gray_premul{0.3f, 0.3f, 0.3f, 0.6f};

    // NOTE: In premultiplied Porter-Duff compositing, the identities
    // "Multiply with white → source" etc. only hold in straight alpha
    // without the over compositing.  Here B(Cb, Cs) is the blend function
    // on straight RGB, but the final output is:
    //   Co = cb*(1-As) + cs*(1-Ab) + B(Cb,Cs)*(As*Ab)
    //
    // For cs={0.3,0.3,0.3,0.6} with white ({1,1,1,1}) Multiply:
    //   Cs=0.5, Cb=1.0 → B=0.5*1.0=0.5
    //   Co = 1.0*0.4 + 0.3*0.0 + 0.5*0.6 = 0.4 + 0.3 = {0.7,0.7,0.7,1.0}
    Color r = blend_reference_premul(gray_premul, white, BlendMode::Multiply);
    check_color_close(r, {0.7f, 0.7f, 0.7f, 1.0f});

    // Screen with black: B = Cs+0-Cs*0 = 0.5 → Co = 0 + 0.3*0 + 0.5*0.6 = {0.3,0.3,0.3,1.0}
    r = blend_reference_premul(gray_premul, black, BlendMode::Screen);
    check_color_close(r, {0.3f, 0.3f, 0.3f, 1.0f});

    // Difference with black: B = |0.5-0| = 0.5 → same as Screen
    r = blend_reference_premul(gray_premul, black, BlendMode::Difference);
    check_color_close(r, {0.3f, 0.3f, 0.3f, 1.0f});

    // Darken with white: B = min(0.5, 1.0) = 0.5 → same as Multiply
    r = blend_reference_premul(gray_premul, white, BlendMode::Darken);
    check_color_close(r, {0.7f, 0.7f, 0.7f, 1.0f});

    // Lighten with black: B = max(0.5, 0) = 0.5 → same as Screen
    r = blend_reference_premul(gray_premul, black, BlendMode::Lighten);
    check_color_close(r, {0.3f, 0.3f, 0.3f, 1.0f});
}

TEST_CASE("blend: same image Difference/Exclusion with over compositing") {
    // In premultiplied Porter-Duff, Difference(same, same) ≠ 0 because:
    //   Co = cb*(1-As) + cs*(1-Ab) + |Cs-Cs|*(As*Ab) = 2*cb*(1-As)
    // With c={0.4,0.2,0.1,0.5}: Co = 2*{0.4,0.2,0.1}*0.5 = {0.4,0.2,0.1}
    // and Ao = 0.5+0.5-0.25 = 0.75
    Color c{0.4f, 0.2f, 0.1f, 0.5f};
    Color diff = blend_reference_premul(c, c, BlendMode::Difference);
    check_color_close(diff, {0.4f, 0.2f, 0.1f, 0.75f});

    // Exclusion(same,same): B = Cs+Cs-2*Cs*Cs = 2*Cs*(1-Cs) ≠ 0
    // Cs = (0.8, 0.4, 0.2)
    // B_r = 0.8+0.8-2*0.64 = 0.32, B_g = 0.4+0.4-2*0.16 = 0.48, B_b = 0.2+0.2-2*0.04 = 0.32
    // Co.g = 0.2*0.5 + 0.2*0.5 + 0.48*0.25 = 0.1 + 0.1 + 0.12 = 0.32
    Color excl = blend_reference_premul(c, c, BlendMode::Exclusion);
    check_color_close(excl, {0.48f, 0.32f, 0.18f, 0.75f});
}

// ═════════════════════════════════════════════════════════════════════════════
// Universal: Edge cases for B3 modes
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend_b3: HardLight Cs=0.5 boundary — both branches equal") {
    // At Cs=0.5: branch1 = 2*cb*0.5 = cb, branch2 = 1 - 2*(1-cb)*0.5 = 1-(1-cb) = cb
    check_close(blend_hard_light(0.3f, 0.5f), 0.3f);
    check_close(blend_hard_light(0.8f, 0.5f), 0.8f);
}

TEST_CASE("blend_b3: SoftLight Cb=0.25 boundary — continuous") {
    // At Cb=0.25, both d_cb branches produce 0.5:
    // Poly: ((16*0.25-12)*0.25+4)*0.25 = ((4-12)*0.25+4)*0.25 = (-2+4)*0.25 = 0.5
    // Sqrt: sqrt(0.25) = 0.5
    float d = soft_light_d(0.25f);
    check_close(d, 0.5f);
}

TEST_CASE("blend_b3: SoftLight at extremes") {
    // Cb=0, Cs=0.5 → 0 - (1-1)*0*(1-0) = 0
    check_close(blend_soft_light(0.0f, 0.5f), 0.0f);
    // Cb=1, Cs=0.5 → 1 + (1-1)*(d(1)-1) = 1
    check_close(blend_soft_light(1.0f, 0.5f), 1.0f);
}

TEST_CASE("blend_b3: ColorDodge Cs=1 → 1") {
    check_close(blend_color_dodge(0.3f, 1.0f), 1.0f);
}

TEST_CASE("blend_b3: ColorBurn Cs=0 → 0") {
    check_close(blend_color_burn(0.5f, 0.0f), 0.0f);
}

TEST_CASE("blend_b3: ColorDodge Cb=0 → 0") {
    check_close(blend_color_dodge(0.0f, 0.5f), 0.0f);
}

TEST_CASE("blend_b3: ColorBurn Cb=1 → 1") {
    check_close(blend_color_burn(1.0f, 0.3f), 1.0f);
}

// ═════════════════════════════════════════════════════════════════════════════
// Universal: No NaN/Inf for any mode
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend: NaN source returns transparent for all modes") {
    Color nan_src{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f};
    Color dst{0.3f, 0.4f, 0.5f, 0.6f};
    for (int m = 0; m <= static_cast<int>(BlendMode::ColorBurn); ++m) {
        auto mode = static_cast<BlendMode>(m);
        Color r = blend_reference_premul(nan_src, dst, mode);
        check_color_close(r, Color::transparent());
    }
}

TEST_CASE("blend: Inf source returns transparent for all modes") {
    Color inf_src{std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 1.0f};
    Color dst{0.3f, 0.4f, 0.5f, 0.6f};
    for (int m = 0; m <= static_cast<int>(BlendMode::ColorBurn); ++m) {
        auto mode = static_cast<BlendMode>(m);
        Color r = blend_reference_premul(inf_src, dst, mode);
        check_color_close(r, Color::transparent());
    }
}

TEST_CASE("blend: NaN backdrop returns transparent for all modes") {
    Color src{0.4f, 0.1f, 0.2f, 0.5f};
    Color nan_dst{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f};
    for (int m = 0; m <= static_cast<int>(BlendMode::ColorBurn); ++m) {
        auto mode = static_cast<BlendMode>(m);
        Color r = blend_reference_premul(src, nan_dst, mode);
        check_color_close(r, Color::transparent());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Universal: randomized finite check (broad coverage)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("blend: no NaN/Inf for random inputs across all modes") {
    std::mt19937 gen(42);
    std::uniform_real_distribution<float> d(-2.0f, 4.0f);  // HDR range
    std::uniform_real_distribution<float> ad(0.0f, 1.0f);

    for (int m = 0; m <= static_cast<int>(BlendMode::ColorBurn); ++m) {
        auto mode = static_cast<BlendMode>(m);
        for (int i = 0; i < 1000; ++i) {
            float sa = ad(gen);
            float da = ad(gen);
            Color src{d(gen) * sa, d(gen) * sa, d(gen) * sa, sa};
            Color dst{d(gen) * da, d(gen) * da, d(gen) * da, da};
            Color r = blend_reference_premul(src, dst, mode);
            check_finite(r);
        }
    }
}
