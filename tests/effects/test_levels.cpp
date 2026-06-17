// ---------------------------------------------------------------------------
// test_levels.cpp — Levels test (sezione 7 specifica)
// ---------------------------------------------------------------------------
//
// Copertura (sezione 7):
//   7.1  Identità: input_black=0, input_white=1, gamma=1, out_black=0, out_white=1
//   7.2  Black/white remap: stretch [0.25, 0.75] → [0, 1]
//   7.3  Gamma: gamma=2 → sqrt, gamma=0.5 → square
//   7.4  Output remapping: output_black/output_white shift/stretch
//   7.5  Per-channel: solo rosso modificato, G/B invariati
//   7.6  Master + per-channel combinati
//   7.7  HDR: input > 1 extrapolated via slope
//   7.8  HDR output: output_white > 1 per HDR output
//   7.9  Alpha preservata: varie alpha
//   7.10 Trasparente: pixel alpha=0 skip
//   7.11 Clip: pixel fuori dal clip invariati
//   7.12 ColorPipeline: LevelsStage fuso con ExposureStage
//   7.13 Gamma estremo: gamma=0.1 (inverse quasi-step) e gamma=10 (quasi-linear)

#include <doctest/doctest.h>
#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include "src/backends/software/processors/effects/color/exposure_levels.hpp"
#include "tests/effects/test_helpers.hpp"
using namespace test_fx;
#include <cmath>
using namespace chronon3d;

using namespace chronon3d::renderer;

// =============================================================================
// 7.1  Identità (sezione 7.1)
// =============================================================================

TEST_CASE("Levels: identity preserves values -0.5 to 2.0") {
    const float inputs[] = {-0.5f, 0.0f, 0.25f, 0.5f, 1.0f, 2.0f};
    Framebuffer fb(6, 1);
    for (int x = 0; x < 6; ++x)
        fb.set_pixel(x, 0, Color{inputs[x], inputs[x], inputs[x], 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,   // master identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,   // red identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,   // green identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);  // blue identity

    for (int x = 0; x < 6; ++x) {
        CHECK(fb.get_pixel(x, 0).r == doctest::Approx(inputs[x]).epsilon(kScalarEpsilon));
        CHECK(fb.get_pixel(x, 0).g == doctest::Approx(inputs[x]).epsilon(kScalarEpsilon));
        CHECK(fb.get_pixel(x, 0).b == doctest::Approx(inputs[x]).epsilon(kScalarEpsilon));
    }
}

// =============================================================================
// 7.2  Black/white remap (sezione 7.2)
// =============================================================================

TEST_CASE("Levels: stretch [0.25, 0.75] to [0, 1]") {
    Framebuffer fb(4, 1);
    fb.set_pixel(0, 0, Color{0.25f, 0.25f, 0.25f, 1.0f});
    fb.set_pixel(1, 0, Color{0.50f, 0.50f, 0.50f, 1.0f});
    fb.set_pixel(2, 0, Color{0.75f, 0.75f, 0.75f, 1.0f});
    fb.set_pixel(3, 0, Color{1.00f, 1.00f, 1.00f, 1.0f});

    apply_levels(fb,
                 0.25f, 0.75f, 1.0f, 0.0f, 1.0f,  // master
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,     // red identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,     // green identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);    // blue identity

    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(0.0f).epsilon(kScalarEpsilon));
    CHECK(fb.get_pixel(1, 0).r == doctest::Approx(0.5f).epsilon(kScalarEpsilon));
    CHECK(fb.get_pixel(2, 0).r == doctest::Approx(1.0f).epsilon(kScalarEpsilon));
    // HDR extrapolation: (1.0 - 0.25) / 0.5 = 1.5
    CHECK(fb.get_pixel(3, 0).r == doctest::Approx(1.5f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 7.3  Gamma (sezione 7.3)
// =============================================================================

TEST_CASE("Levels: gamma=2, input 0.5 → sqrt(0.5)") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{0.5f, 0.5f, 0.5f, 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 2.0f, 0.0f, 1.0f,   // master gamma=2
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    float expected = std::sqrt(0.5f);  // ≈ 0.70710678
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(expected).epsilon(kScalarEpsilon));
}

TEST_CASE("Levels: gamma=0.5, input 0.5 → pow(0.5, 2) = 0.25") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{0.5f, 0.5f, 0.5f, 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 0.5f, 0.0f, 1.0f,   // master gamma=0.5
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    float expected = 0.5f * 0.5f;  // pow(0.5, 1/0.5) = pow(0.5, 2) = 0.25
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(expected).epsilon(kScalarEpsilon));
}

// =============================================================================
// 7.4  Output remapping (sezione 7.4)
// =============================================================================

TEST_CASE("Levels: output_black=0.2, output_white=0.8 compresses to [0.2, 0.8]") {
    Framebuffer fb(3, 1);
    fb.set_pixel(0, 0, Color{0.0f, 0.0f, 0.0f, 1.0f});
    fb.set_pixel(1, 0, Color{0.5f, 0.5f, 0.5f, 1.0f});
    fb.set_pixel(2, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 1.0f, 0.2f, 0.8f,  // master: out_black=0.2, out_white=0.8
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(0.2f).epsilon(kScalarEpsilon));
    CHECK(fb.get_pixel(1, 0).r == doctest::Approx(0.5f).epsilon(kScalarEpsilon));
    CHECK(fb.get_pixel(2, 0).r == doctest::Approx(0.8f).epsilon(kScalarEpsilon));
}

TEST_CASE("Levels: output_white=2.0 produces HDR output") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 1.0f, 0.0f, 2.0f,  // master: out_white=2.0
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(2.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 7.5  Per-channel (sezione 7.5)
// =============================================================================

TEST_CASE("Levels: only red channel modified") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{0.2f, 0.4f, 0.6f, 1.0f});

    // Red: input_white=0.5 → R = 0.2 / 0.5 = 0.4
    apply_levels(fb,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,   // master identity
                 0.0f, 0.5f, 1.0f, 0.0f, 1.0f,   // red: in_white=0.5
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,   // green identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);  // blue identity

    Color out = fb.get_pixel(0, 0);
    CHECK(out.r == doctest::Approx(0.4f).epsilon(kScalarEpsilon));
    CHECK(out.g == doctest::Approx(0.4f).epsilon(kScalarEpsilon));
    CHECK(out.b == doctest::Approx(0.6f).epsilon(kScalarEpsilon));
}

TEST_CASE("Levels: only green channel modified") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{0.3f, 0.5f, 0.7f, 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.2f, 0.8f, 1.0f, 0.0f, 1.0f,  // green: [0.2, 0.8]→[0, 1]
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    // G: (0.5 - 0.2) / (0.8 - 0.2) = 0.3 / 0.6 = 0.5
    Color out = fb.get_pixel(0, 0);
    CHECK(out.r == doctest::Approx(0.3f).epsilon(kScalarEpsilon));  // unchanged
    CHECK(out.g == doctest::Approx(0.5f).epsilon(kScalarEpsilon));
    CHECK(out.b == doctest::Approx(0.7f).epsilon(kScalarEpsilon));  // unchanged
}

// =============================================================================
// 7.6  Master + per-channel combinati (sezione 7.6)
// =============================================================================

TEST_CASE("Levels: master gamma=2 + red input_white=0.5 combine correctly") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{0.5f, 0.5f, 0.5f, 1.0f});

    // Red: in_white=0.5 → 0.5/0.5 = 1.0, then master gamma=2 → sqrt(1.0) = 1.0
    // Green: master gamma=2 → sqrt(0.5) ≈ 0.707
    apply_levels(fb,
                 0.0f, 1.0f, 2.0f, 0.0f, 1.0f,  // master gamma=2
                 0.0f, 0.5f, 1.0f, 0.0f, 1.0f,  // red: in_white=0.5
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,  // green identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f); // blue identity

    Color out = fb.get_pixel(0, 0);
    // Red: first per-channel: 0.5/0.5 = 1.0, then master gamma=2: sqrt(1.0) = 1.0
    CHECK(out.r == doctest::Approx(1.0f).epsilon(kScalarEpsilon));
    // Green: only master gamma=2: sqrt(0.5)
    float sqrt_half = std::sqrt(0.5f);
    CHECK(out.g == doctest::Approx(sqrt_half).epsilon(kScalarEpsilon));
    // Blue: only master gamma=2: sqrt(0.5)
    CHECK(out.b == doctest::Approx(sqrt_half).epsilon(kScalarEpsilon));
}

// =============================================================================
// 7.7  HDR extrapolation (sezione 7.7)
// =============================================================================

TEST_CASE("Levels: HDR input > 1 extrapolated by slope") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{2.0f, 2.0f, 2.0f, 1.0f});

    // input_black=0, input_white=1, gamma=1, out_black=0, out_white=1
    // 2.0 → (2.0-0)/(1-0) = 2.0 → pow(2.0, 1/1) = 2.0 → 0 + 2.0*1 = 2.0
    apply_levels(fb,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(2.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 7.8  HDR output (sezione 7.8)
// =============================================================================

TEST_CASE("Levels: output_white=2.0 with HDR input preserves no clamp") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{2.0f, 2.0f, 2.0f, 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 1.0f, 0.0f, 2.0f,  // out_white=2.0
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    // (2.0 - 0) / 1.0 = 2.0 → pow(2.0, 1) = 2.0 → 0 + 2.0 * 2.0 = 4.0
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(4.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 7.9  Alpha preservata (sezione 7.9)
// =============================================================================

TEST_CASE("Levels: alpha preserved for various inputs") {
    auto test_alpha = [](Color input) {
        Framebuffer fb(1, 1);
        fb.set_pixel(0, 0, input);
        apply_levels(fb,
                     0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                     0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                     0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                     0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        CHECK(fb.get_pixel(0, 0).a == doctest::Approx(input.a).epsilon(kExactEpsilon));
    };

    test_alpha(kHalfAlphaPremul);   // alpha 0.5
    test_alpha(kHdrHalfPremul);     // alpha 0.5 HDR
    test_alpha(kOpaquePremul);      // alpha 1.0
    test_alpha(kNegativePremul);    // alpha 1.0
}

TEST_CASE("Levels: alpha preserved with non-identity params") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHalfAlphaPremul);  // {0.4, 0.2, 0.1, 0.5}

    apply_levels(fb,
                 0.0f, 0.5f, 1.0f, 0.0f, 1.0f,  // master: raddoppia
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    // Premultiplied: 0.8/0.5 = 1.6 → premul: 1.6*0.5 = 0.8
    CHECK(fb.get_pixel(0, 0).a == doctest::Approx(0.5f).epsilon(kExactEpsilon));
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(0.8f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 7.10 Trasparente (sezione 7.10)
// =============================================================================

TEST_CASE("Levels: transparent input stays transparent") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color::transparent());

    apply_levels(fb,
                 0.2f, 0.8f, 0.5f, 0.1f, 0.9f,
                 0.2f, 0.8f, 0.5f, 0.1f, 0.9f,
                 0.2f, 0.8f, 0.5f, 0.1f, 0.9f,
                 0.2f, 0.8f, 0.5f, 0.1f, 0.9f);

    CHECK(fb.get_pixel(0, 0) == Color::transparent());
}

// =============================================================================
// 7.11 Clip (sezione 7.11)
// =============================================================================

TEST_CASE("Levels: clip preserves pixels outside clip region") {
    Framebuffer fb = make_coord_fb(8, 8);
    Framebuffer original = make_coord_fb(8, 8);

    // Apply with partial clip [2, 3] → [6, 7]
    apply_levels(fb,
                 0.0f, 0.5f, 1.0f, 0.0f, 1.0f,   // master: raddoppia
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 raster::BBox{2, 3, 6, 7});

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            bool inside = (x >= 2 && x < 6 && y >= 3 && y < 7);
            if (!inside) {
                check_color_near(fb.get_pixel(x, y),
                                 original.get_pixel(x, y),
                                 kExactEpsilon);
            }
        }
    }
}

// =============================================================================
// 7.12 ColorPipeline fusion (sezione 7.12)
// =============================================================================

TEST_CASE("ColorPipeline: LevelsStage fused with ExposureStage") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHalfAlphaPremul);  // {0.4, 0.2, 0.1, 0.5}

    // Pipeline: Levels stretch → Exposure +1 stop
    {
        ColorPipeline pipeline;
        LevelsStage lvl;
        lvl.master = LevelsChannelStage{0.0f, 0.5f, 1.0f, 0.0f, 1.0f};  // raddoppia
        pipeline.add_stage(lvl);
        pipeline.add_stage(ExposureStage{1.0f});
        pipeline.apply(fb);
    }

    // Reference: sequential
    Framebuffer ref(1, 1);
    ref.set_pixel(0, 0, kHalfAlphaPremul);
    apply_levels(ref,
                 0.0f, 0.5f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    apply_exposure(ref, 1.0f);

    check_color_near(fb.get_pixel(0, 0), ref.get_pixel(0, 0), kScalarEpsilon);
}

// =============================================================================
// 7.13 Gamma estremo (sezione 7.13)
// =============================================================================

TEST_CASE("Levels: gamma=0.1, near step function") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{0.4f, 0.4f, 0.4f, 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 0.1f, 0.0f, 1.0f,  // gamma=0.1
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    // pow(0.4, 1/0.1) = pow(0.4, 10) = 0.0001048576
    float expected = std::pow(0.4f, 10.0f);
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(expected).epsilon(kScalarEpsilon));
    CHECK(std::isfinite(fb.get_pixel(0, 0).r));
}

TEST_CASE("Levels: gamma=10, near linear but compressed") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{0.4f, 0.4f, 0.4f, 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 10.0f, 0.0f, 1.0f,  // gamma=10
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    // pow(0.4, 1/10) = pow(0.4, 0.1) ≈ 0.9124
    float expected = std::pow(0.4f, 0.1f);
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(expected).epsilon(kScalarEpsilon));
    CHECK(std::isfinite(fb.get_pixel(0, 0).r));
}

// =============================================================================
// 7.14 Negative input (sezione 7.14)
// =============================================================================

TEST_CASE("Levels: negative input values preserved via signed pow") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kNegativePremul);  // {-0.2, 0.3, 1.5, 1.0}

    apply_levels(fb,
                 0.0f, 1.0f, 2.0f, 0.0f, 1.0f,   // master gamma=2
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);

    // R = -0.2: signed pow: -pow(0.2, 0.5) = -sqrt(0.2) ≈ -0.447
    // G = 0.3: pow(0.3, 0.5) = sqrt(0.3) ≈ 0.548
    // B = 1.5: HDR extrapolation — but gamma=2, so:
    //   n = 1.5/1 = 1.5 → pow(1.5, 0.5) = sqrt(1.5) ≈ 1.225
    float expected_r = -std::sqrt(0.2f);
    float expected_g = std::sqrt(0.3f);
    float expected_b = std::sqrt(1.5f);

    Color out = fb.get_pixel(0, 0);
    CHECK(out.r == doctest::Approx(expected_r).epsilon(kScalarEpsilon));
    CHECK(out.g == doctest::Approx(expected_g).epsilon(kScalarEpsilon));
    CHECK(out.b == doctest::Approx(expected_b).epsilon(kScalarEpsilon));
}
