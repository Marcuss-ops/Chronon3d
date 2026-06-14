// ---------------------------------------------------------------------------
// test_compose_color_op.cpp — ComposeColorOp tests (sezione 2 specifica)
// ---------------------------------------------------------------------------
//
// Copertura:
//   1. Multiply identity (blend = 1) → identità
//   2. Multiply 2× (blend = 2) → raddoppio
//   3. Screen con blend = 0.5
//   4. Overlay: blend < 0.5 e blend >= 0.5
//   5. Add: HDR-safe
//   6. Subtract: può andare negativo
//   7. Difference: abs(src - blend)
//   8. Exposure via compose: +1 stop ≡ Multiply(2)
//   9. Alpha preserved invariata
//   10. HDR preserved
//   11. ComposeStage in ColorPipeline
//   12. apply_compose_to_buffer identity

#include <doctest/doctest.h>
#include <chronon3d/effects/compose_color_op.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/effects/color_contract.hpp>
#include "src/backends/software/processors/effects/color/exposure_levels.hpp"
#include "tests/effects/test_helpers.hpp"
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::color;
using namespace chronon3d::renderer;

// =============================================================================
// 1. ComposeOp::Multiply — identity (section 2.1)
// =============================================================================

TEST_CASE("ComposeOp::Multiply: blend=1 is identity") {
    StraightRgb inputs[] = {
        {0.0f, 0.0f, 0.0f},
        {0.5f, 0.3f, 0.7f},
        {1.0f, 1.0f, 1.0f},
        {2.0f, 0.5f, 4.0f}   // HDR
    };
    StraightRgb blend{1.0f, 1.0f, 1.0f};

    for (const auto& src : inputs) {
        auto result = apply_compose_op(src, ComposeOp::Multiply, blend);
        CHECK(result.r == doctest::Approx(src.r).epsilon(kExactEpsilon));
        CHECK(result.g == doctest::Approx(src.g).epsilon(kExactEpsilon));
        CHECK(result.b == doctest::Approx(src.b).epsilon(kExactEpsilon));
    }
}

// =============================================================================
// 2. ComposeOp::Multiply — 2× (section 2.2)
// =============================================================================

TEST_CASE("ComposeOp::Multiply: blend=2 doubles values") {
    StraightRgb result = apply_compose_op(
        {0.5f, 0.3f, 0.7f},
        ComposeOp::Multiply,
        {2.0f, 2.0f, 2.0f}
    );
    CHECK(result.r == doctest::Approx(1.0f).epsilon(kScalarEpsilon));
    CHECK(result.g == doctest::Approx(0.6f).epsilon(kScalarEpsilon));
    CHECK(result.b == doctest::Approx(1.4f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 3. ComposeOp::Screen (section 2.3)
// =============================================================================

TEST_CASE("ComposeOp::Screen: with blend=0.5") {
    StraightRgb result = apply_compose_op(
        {0.2f, 0.4f, 0.6f},
        ComposeOp::Screen,
        {0.5f, 0.5f, 0.5f}
    );
    // 1 - (1-0.2)*(1-0.5) = 1 - 0.8*0.5 = 1 - 0.4 = 0.6
    // 1 - (1-0.4)*(1-0.5) = 1 - 0.6*0.5 = 1 - 0.3 = 0.7
    // 1 - (1-0.6)*(1-0.5) = 1 - 0.4*0.5 = 1 - 0.2 = 0.8
    CHECK(result.r == doctest::Approx(0.6f).epsilon(kScalarEpsilon));
    CHECK(result.g == doctest::Approx(0.7f).epsilon(kScalarEpsilon));
    CHECK(result.b == doctest::Approx(0.8f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 4. ComposeOp::Overlay (section 2.4)
// =============================================================================

TEST_CASE("ComposeOp::Overlay: blend below/above 0.5") {
    // blend = 0.25 (< 0.5): 2 * src * blend
    StraightRgb r1 = apply_compose_op(
        {0.8f, 0.0f, 0.0f},
        ComposeOp::Overlay,
        {0.25f, 0.25f, 0.25f}
    );
    CHECK(r1.r == doctest::Approx(2.0f * 0.8f * 0.25f).epsilon(kScalarEpsilon)); // 0.4

    // blend = 0.75 (>= 0.5): 1 - 2*(1-src)*(1-blend)
    StraightRgb r2 = apply_compose_op(
        {0.8f, 0.0f, 0.0f},
        ComposeOp::Overlay,
        {0.75f, 0.75f, 0.75f}
    );
    float expected = 1.0f - 2.0f * (1.0f - 0.8f) * (1.0f - 0.75f); // 1 - 2*0.2*0.25 = 0.9
    CHECK(r2.r == doctest::Approx(expected).epsilon(kScalarEpsilon));
}

// =============================================================================
// 5. ComposeOp::Add — HDR-safe (section 2.5)
// =============================================================================

TEST_CASE("ComposeOp::Add: no clamp, HDR preserved") {
    StraightRgb result = apply_compose_op(
        {2.0f,  -0.5f,  1.5f},
        ComposeOp::Add,
        {3.0f,   0.5f, -0.5f}
    );
    CHECK(result.r == doctest::Approx(5.0f).epsilon(kScalarEpsilon));   // 2 + 3 = 5
    CHECK(result.g == doctest::Approx(0.0f).epsilon(kScalarEpsilon));  // -0.5 + 0.5 = 0
    CHECK(result.b == doctest::Approx(1.0f).epsilon(kScalarEpsilon));  // 1.5 - 0.5 = 1.0
}

// =============================================================================
// 6. ComposeOp::Subtract — can go negative (section 2.6)
// =============================================================================

TEST_CASE("ComposeOp::Subtract: negative values allowed") {
    StraightRgb result = apply_compose_op(
        {0.3f, 0.5f, 1.0f},
        ComposeOp::Subtract,
        {0.5f, 0.2f, 2.0f}
    );
    CHECK(result.r == doctest::Approx(-0.2f).epsilon(kScalarEpsilon));
    CHECK(result.g == doctest::Approx(0.3f).epsilon(kScalarEpsilon));
    CHECK(result.b == doctest::Approx(-1.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 7. ComposeOp::Difference (section 2.7)
// =============================================================================

TEST_CASE("ComposeOp::Difference: abs of differences") {
    StraightRgb result = apply_compose_op(
        {0.8f, 0.2f, 0.5f},
        ComposeOp::Difference,
        {0.3f, 0.7f, 0.5f}
    );
    CHECK(result.r == doctest::Approx(0.5f).epsilon(kScalarEpsilon));  // |0.8-0.3| = 0.5
    CHECK(result.g == doctest::Approx(0.5f).epsilon(kScalarEpsilon));  // |0.2-0.7| = 0.5
    CHECK(result.b == doctest::Approx(0.0f).epsilon(kScalarEpsilon));  // |0.5-0.5| = 0.0
}

// =============================================================================
// 8. Exposure ≡ ComposeOp::Multiply with 2^stops (section 2.8)
// =============================================================================

TEST_CASE("Exposure via ComposeOp::Multiply: +1 stop = Multiply(2)") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHalfAlphaPremul);  // {0.4, 0.2, 0.1, 0.5}

    // Exposure +1 stop
    {
        Framebuffer fb2(1, 1);
        fb2.set_pixel(0, 0, kHalfAlphaPremul);
        apply_exposure(fb2, 1.0f);
        Color expected = fb2.get_pixel(0, 0);

        // Same via ComposeOp::Multiply directly
        Framebuffer fb3(1, 1);
        fb3.set_pixel(0, 0, kHalfAlphaPremul);
        apply_compose_to_buffer(fb3, ComposeOp::Multiply, {2.0f, 2.0f, 2.0f});

        check_color_near(fb3.get_pixel(0, 0), expected, kScalarEpsilon);
    }
}

TEST_CASE("Exposure via ComposeOp::Multiply: -1 stop = Multiply(0.5)") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kOpaquePremul);  // {0.8, 0.4, 0.2, 1.0}

    // Exposure -1 stop
    Framebuffer fb2(1, 1);
    fb2.set_pixel(0, 0, kOpaquePremul);
    apply_exposure(fb2, -1.0f);
    Color expected = fb2.get_pixel(0, 0);

    // Same via ComposeOp::Multiply
    Framebuffer fb3(1, 1);
    fb3.set_pixel(0, 0, kOpaquePremul);
    apply_compose_to_buffer(fb3, ComposeOp::Multiply, {0.5f, 0.5f, 0.5f});

    check_color_near(fb3.get_pixel(0, 0), expected, kScalarEpsilon);
}

// =============================================================================
// 9. Alpha preserved (section 2.9)
// =============================================================================

TEST_CASE("ComposeOp: alpha preserved for all operations") {
    auto test_op = [](ComposeOp op, StraightRgb blend) {
        Framebuffer fb(1, 1);
        fb.set_pixel(0, 0, kHalfAlphaPremul);
        apply_compose_to_buffer(fb, op, blend);
        Color output = fb.get_pixel(0, 0);
        // Alpha must be exactly preserved (premultiply_rgb preserves it)
        CHECK(output.a == doctest::Approx(0.5f).epsilon(kExactEpsilon));
        CHECK(std::isfinite(output.r));
        CHECK(std::isfinite(output.g));
        CHECK(std::isfinite(output.b));
    };

    test_op(ComposeOp::Multiply,   {2.0f, 2.0f, 2.0f});
    test_op(ComposeOp::Screen,     {0.5f, 0.5f, 0.5f});
    test_op(ComposeOp::Overlay,    {0.3f, 0.7f, 0.5f});
    test_op(ComposeOp::Add,        {0.3f, 0.3f, 0.3f});
    test_op(ComposeOp::Subtract,   {0.1f, 0.1f, 0.1f});
    test_op(ComposeOp::Difference, {0.5f, 0.5f, 0.5f});
}

// =============================================================================
// 10. HDR preserved (section 2.10)
// =============================================================================

TEST_CASE("ComposeOp: HDR values no accidental clamp") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHdrPremul);  // {2.0, 0.5, 4.0, 1.0}

    apply_compose_to_buffer(fb, ComposeOp::Multiply, {2.0f, 1.0f, 0.5f});

    Color output = fb.get_pixel(0, 0);
    // Straight: {2*2=4, 0.5*1=0.5, 4*0.5=2, a=1}
    // Premultiplied: same (alpha=1)
    CHECK(output.r == doctest::Approx(4.0f).epsilon(kScalarEpsilon));
    CHECK(output.g == doctest::Approx(0.5f).epsilon(kScalarEpsilon));
    CHECK(output.b == doctest::Approx(2.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 11. ComposeStage in ColorPipeline (section 2.11)
// =============================================================================

TEST_CASE("ColorPipeline: ComposeStage fuses with other stages") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHalfAlphaPremul);  // {0.4, 0.2, 0.1, 0.5}

    // Pipeline: Exposure +1 stop → ComposeStage Screen(0.5)
    ColorPipeline pipeline;
    pipeline.add_stage(ExposureStage{1.0f});
    pipeline.add_stage(ComposeStage{ComposeOp::Screen, {0.5f, 0.5f, 0.5f}});
    pipeline.apply(fb);

    // Reference: apply sequentially
    Framebuffer ref(1, 1);
    ref.set_pixel(0, 0, kHalfAlphaPremul);
    apply_exposure(ref, 1.0f);
    apply_compose_to_buffer(ref, ComposeOp::Screen, {0.5f, 0.5f, 0.5f});

    check_color_near(fb.get_pixel(0, 0), ref.get_pixel(0, 0), kScalarEpsilon);
}

// =============================================================================
// 12. apply_compose_to_buffer — identity when multiply by 1 (section 2.12)
// =============================================================================

TEST_CASE("ComposeOp: multiply by 1 is identity on rampa") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_compose_to_buffer(fb, ComposeOp::Multiply, {1.0f, 1.0f, 1.0f});

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

TEST_CASE("ComposeOp: transparent input stays transparent") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color::transparent());

    apply_compose_to_buffer(fb, ComposeOp::Multiply, {5.0f, 5.0f, 5.0f});

    CHECK(fb.get_pixel(0, 0) == Color::transparent());
}

TEST_CASE("ComposeOp: Divide by 1 is identity") {
    StraightRgb result = apply_compose_op(
        {0.5f, 0.3f, 0.7f},
        ComposeOp::Divide,
        {1.0f, 1.0f, 1.0f}
    );
    CHECK(result.r == doctest::Approx(0.5f).epsilon(kExactEpsilon));
    CHECK(result.g == doctest::Approx(0.3f).epsilon(kExactEpsilon));
    CHECK(result.b == doctest::Approx(0.7f).epsilon(kExactEpsilon));
}

TEST_CASE("ComposeOp: DarkenOnly and LightenOnly") {
    StraightRgb src{0.2f, 0.8f, 0.5f};
    StraightRgb blend{0.6f, 0.3f, 0.5f};

    StraightRgb dark = apply_compose_op(src, ComposeOp::DarkenOnly, blend);
    CHECK(dark.r == doctest::Approx(0.2f).epsilon(kExactEpsilon));  // min(0.2, 0.6)
    CHECK(dark.g == doctest::Approx(0.3f).epsilon(kExactEpsilon));  // min(0.8, 0.3)
    CHECK(dark.b == doctest::Approx(0.5f).epsilon(kExactEpsilon));  // min(0.5, 0.5)

    StraightRgb light = apply_compose_op(src, ComposeOp::LightenOnly, blend);
    CHECK(light.r == doctest::Approx(0.6f).epsilon(kExactEpsilon)); // max(0.2, 0.6)
    CHECK(light.g == doctest::Approx(0.8f).epsilon(kExactEpsilon)); // max(0.8, 0.3)
    CHECK(light.b == doctest::Approx(0.5f).epsilon(kExactEpsilon)); // max(0.5, 0.5)
}
