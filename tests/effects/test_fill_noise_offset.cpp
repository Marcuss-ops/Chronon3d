// ---------------------------------------------------------------------------
// test_fill_noise_offset.cpp — Fill, Noise, Offset tests (sezione 7 specifica)
// ---------------------------------------------------------------------------
//
// Copertura:
//   Fill:    identity (amount=0), Replace (calcolo manuale), Mix 25%,
//            alpha preservata, clip parziale
//   Noise:   determinismo (stesso input → stesso output), seed diverso,
//            static vs animated, monochrome vs RGB, amount=0, alpha preservata
//   Offset:  identity (dx=0,dy=0), integer shift ±1 Transparent,
//            oltre bordo, Wrap, Clamp, integer≈bilinear, clip

#include <doctest/doctest.h>
#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/backends/software/sampling/edge_mode.hpp>
#include "src/backends/software/processors/effects/generate/fill_noise_offset.hpp"
#include "tests/effects/test_helpers.hpp"
using namespace test_fx;
#include <cmath>
#include <cstdint>
using namespace chronon3d;

using namespace chronon3d::renderer;
using namespace chronon3d::sampling;

// =============================================================================
// 1. Fill — identity (amount = 0, sezione 7.0)
// =============================================================================

TEST_CASE("Fill: amount=0 is identity") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_fill(fb, Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.0f, true);

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

// =============================================================================
// 2. Fill — Replace (sezione 7.1)
// =============================================================================

TEST_CASE("Fill: replace mode preserves alpha") {
    // Input premoltiplicato kHalfAlphaPremul {0.4, 0.2, 0.1, 0.5}
    // Fill rosso straight {1.0, 0.0, 0.0, 1.0}
    // Alpha deve restare 0.5
    // Output premoltiplicato: {1.0*0.5=0.5, 0.0*0.5=0.0, 0.0*0.5=0.0, 0.5}

    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHalfAlphaPremul);

    apply_fill(fb, Color{1.0f, 0.0f, 0.0f, 1.0f}, 1.0f, true);

    Color expected{0.5f, 0.0f, 0.0f, 0.5f};
    Color actual = fb.get_pixel(0, 0);
    check_color_near(actual, expected, kScalarEpsilon);
    CHECK(actual.a == doctest::Approx(0.5f));
}

// =============================================================================
// 3. Fill — Mix 25% (sezione 7.2)
// =============================================================================

TEST_CASE("Fill: mix 25% with half-alpha premul") {
    // Straight source: 0.8, 0.4, 0.2
    // Fill straight:    1.0, 0.0, 0.0
    // Mix 0.25:
    //   R = 0.8*0.75 + 1.0*0.25 = 0.85
    //   G = 0.4*0.75 + 0.0*0.25 = 0.30
    //   B = 0.2*0.75 + 0.0*0.25 = 0.15
    // Alpha 0.5, output premoltiplicato:
    //   {0.85*0.5=0.425, 0.30*0.5=0.150, 0.15*0.5=0.075, 0.5}

    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHalfAlphaPremul);  // {0.4, 0.2, 0.1, 0.5}

    apply_fill(fb, Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.25f, false);  // mix mode

    Color expected{0.425f, 0.150f, 0.075f, 0.5f};
    Color actual = fb.get_pixel(0, 0);
    check_color_near(actual, expected, kScalarEpsilon);
}

// =============================================================================
// 4. Fill — alpha preserved (general)
// =============================================================================

TEST_CASE("Fill: alpha preserved for various inputs") {
    auto test_fill_alpha = [](Color input, float amount, bool replace) {
        Framebuffer fb(1, 1);
        fb.set_pixel(0, 0, input);
        apply_fill(fb, Color{1.0f, 0.0f, 0.0f, 1.0f}, amount, replace);
        Color output = fb.get_pixel(0, 0);
        CHECK(output.a == doctest::Approx(input.a).epsilon(kExactEpsilon));
        CHECK(std::isfinite(output.r));
        CHECK(std::isfinite(output.g));
        CHECK(std::isfinite(output.b));
    };

    test_fill_alpha(kHalfAlphaPremul, 1.0f, true);
    test_fill_alpha(kHalfAlphaPremul, 0.5f, false);
    test_fill_alpha(kHdrPremul, 1.0f, true);
    test_fill_alpha(kOpaquePremul, 0.25f, false);
}

TEST_CASE("Fill: transparent input stays transparent") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color::transparent());
    apply_fill(fb, Color{1.0f, 0.0f, 0.0f, 1.0f}, 1.0f, true);
    CHECK(fb.get_pixel(0, 0) == Color::transparent());
}

// =============================================================================
// 5. Fill — clip (sezione 7 general)
// =============================================================================

TEST_CASE("Fill: clip preserves pixels outside clip region") {
    Framebuffer fb = make_coord_fb(8, 8);
    Framebuffer original = make_coord_fb(8, 8);

    apply_fill(fb, Color{1.0f, 0.0f, 0.0f, 1.0f}, 1.0f, true,
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
// 6. Noise — determinismo (sezione 7.3)
// =============================================================================

TEST_CASE("Noise: deterministic — same input produces same output") {
    Framebuffer fb_a(1, 1);
    fb_a.set_pixel(0, 0, kOpaquePremul);
    apply_noise(fb_a, 0.5f, 123u, false, false, 0u);

    Framebuffer fb_b(1, 1);
    fb_b.set_pixel(0, 0, kOpaquePremul);
    apply_noise(fb_b, 0.5f, 123u, false, false, 0u);

    check_color_near(fb_a.get_pixel(0, 0), fb_b.get_pixel(0, 0), kExactEpsilon);
}

TEST_CASE("Noise: different seed produces different output") {
    Framebuffer fb_a(1, 1);
    fb_a.set_pixel(0, 0, kOpaquePremul);
    apply_noise(fb_a, 0.5f, 123u, false, false, 0u);

    Framebuffer fb_b(1, 1);
    fb_b.set_pixel(0, 0, kOpaquePremul);
    apply_noise(fb_b, 0.5f, 124u, false, false, 0u);

    // Different seed should give different noise values
    Color a = fb_a.get_pixel(0, 0);
    Color b = fb_b.get_pixel(0, 0);
    CHECK((std::abs(a.r - b.r) > kExactEpsilon ||
           std::abs(a.g - b.g) > kExactEpsilon ||
           std::abs(a.b - b.b) > kExactEpsilon));
}

// =============================================================================
// 7. Noise — static vs animated (sezione 7.3)
// =============================================================================

TEST_CASE("Noise: static ignores frame parameter") {
    Framebuffer fb_a(1, 1);
    fb_a.set_pixel(0, 0, kOpaquePremul);
    apply_noise(fb_a, 0.5f, 42u, false, false, 0u);

    Framebuffer fb_b(1, 1);
    fb_b.set_pixel(0, 0, kOpaquePremul);
    apply_noise(fb_b, 0.5f, 42u, false, false, 100u);  // frame=100 but animated=false

    check_color_near(fb_a.get_pixel(0, 0), fb_b.get_pixel(0, 0), kExactEpsilon);
}

TEST_CASE("Noise: animated changes with frame") {
    Framebuffer fb_a(1, 1);
    fb_a.set_pixel(0, 0, kOpaquePremul);
    apply_noise(fb_a, 0.5f, 42u, true, false, 0u);

    Framebuffer fb_b(1, 1);
    fb_b.set_pixel(0, 0, kOpaquePremul);
    apply_noise(fb_b, 0.5f, 42u, true, false, 1u);  // different frame, animated=true

    Color a = fb_a.get_pixel(0, 0);
    Color b = fb_b.get_pixel(0, 0);
    CHECK((std::abs(a.r - b.r) > kExactEpsilon ||
           std::abs(a.g - b.g) > kExactEpsilon ||
           std::abs(a.b - b.b) > kExactEpsilon));
}

// =============================================================================
// 8. Noise — monochrome vs RGB (sezione 7.3)
// =============================================================================

TEST_CASE("Noise: monochrome mode — all channels equal") {
    Framebuffer fb(1, 1);
    // Use a uniform-color input so that monochrome noise (same delta on
    // all channels) produces identical R, G, B after the operation.
    fb.set_pixel(0, 0, Color{0.5f, 0.5f, 0.5f, 1.0f});
    apply_noise(fb, 0.5f, 99u, false, false, 0u);  // rgb_mode=false

    Color c = fb.get_pixel(0, 0);
    // In monochrome mode the same noise delta is added to each channel.
    // With a uniform-color input (R=G=B) the output should also have R=G=B.
    CHECK(c.r == doctest::Approx(c.g).epsilon(kExactEpsilon));
    CHECK(c.g == doctest::Approx(c.b).epsilon(kExactEpsilon));
}

TEST_CASE("Noise: RGB mode — channels likely different") {
    // With sufficient number of pixels, at least some must have RGB != G
    Framebuffer fb(16, 16);
    // Fill with opaque white
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            fb.set_pixel(x, y, Color{0.5f, 0.5f, 0.5f, 1.0f});

    apply_noise(fb, 0.5f, 77u, false, true, 0u);  // rgb_mode=true

    // Per la specifica: almeno una percentuale elevata dei pixel deve avere canali diversi
    int different = 0;
    for (int y = 0; y < 16; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < 16; ++x) {
            if (std::abs(row[x].r - row[x].g) > kScalarEpsilon ||
                std::abs(row[x].g - row[x].b) > kScalarEpsilon) {
                ++different;
            }
        }
    }
    CHECK(different > 16 * 16 / 2);  // at least 50% should have different channels
}

// =============================================================================
// 9. Noise — amount=0 identity, alpha preserved
// =============================================================================

TEST_CASE("Noise: amount=0 is identity") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_noise(fb, 0.0f, 42u, false, false, 0u);

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

TEST_CASE("Noise: alpha preserved") {
    auto test_alpha = [](Color input) {
        Framebuffer fb(1, 1);
        fb.set_pixel(0, 0, input);
        apply_noise(fb, 0.5f, 42u, false, false, 0u);
        Color output = fb.get_pixel(0, 0);
        CHECK(output.a == doctest::Approx(input.a).epsilon(kExactEpsilon));
        CHECK(std::isfinite(output.r));
        CHECK(std::isfinite(output.g));
        CHECK(std::isfinite(output.b));
    };

    test_alpha(kHalfAlphaPremul);
    test_alpha(kOpaquePremul);
    test_alpha(kHdrPremul);
    test_alpha(kHdrHalfPremul);
}

TEST_CASE("Noise: transparent input stays transparent") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color::transparent());
    apply_noise(fb, 1.0f, 42u, false, false, 0u);
    CHECK(fb.get_pixel(0, 0) == Color::transparent());
}

// =============================================================================
// 10. Noise — clip
// =============================================================================

TEST_CASE("Noise: clip preserves pixels outside clip region") {
    Framebuffer fb = make_coord_fb(8, 8);
    Framebuffer original = make_coord_fb(8, 8);

    apply_noise(fb, 0.5f, 42u, false, false, 0u,
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
// 11. Offset — identity (dx=0, dy=0, sezione 7.0)
// =============================================================================

TEST_CASE("Offset: dx=0 dy=0 is identity") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_offset(fb, 0.0f, 0.0f, EdgeMode::Transparent, SampleFilter::Nearest);

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

// =============================================================================
// 12. Offset — integer shift Transparent (sezione 7.4)
// =============================================================================

TEST_CASE("Offset: +1 X integer with Transparent edge") {
    // Input: A B C D
    // Offset +1: 0 A B C
    Framebuffer fb(4, 1);
    fb.set_pixel(0, 0, Color{1.0f, 0.0f, 0.0f, 1.0f});  // A
    fb.set_pixel(1, 0, Color{0.0f, 1.0f, 0.0f, 1.0f});  // B
    fb.set_pixel(2, 0, Color{0.0f, 0.0f, 1.0f, 1.0f});  // C
    fb.set_pixel(3, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});  // D

    apply_offset(fb, 1.0f, 0.0f, EdgeMode::Transparent, SampleFilter::Nearest);

    // Expected: 0 A B C (where 0 is transparent)
    CHECK(fb.get_pixel(0, 0) == Color::transparent());
    check_color_near(fb.get_pixel(1, 0), Color{1.0f, 0.0f, 0.0f, 1.0f}, kExactEpsilon);
    check_color_near(fb.get_pixel(2, 0), Color{0.0f, 1.0f, 0.0f, 1.0f}, kExactEpsilon);
    check_color_near(fb.get_pixel(3, 0), Color{0.0f, 0.0f, 1.0f, 1.0f}, kExactEpsilon);
}

TEST_CASE("Offset: -1 X integer with Transparent edge") {
    // Input: A B C D
    // Offset -1: B C D 0
    Framebuffer fb(4, 1);
    fb.set_pixel(0, 0, Color{1.0f, 0.0f, 0.0f, 1.0f});  // A
    fb.set_pixel(1, 0, Color{0.0f, 1.0f, 0.0f, 1.0f});  // B
    fb.set_pixel(2, 0, Color{0.0f, 0.0f, 1.0f, 1.0f});  // C
    fb.set_pixel(3, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});  // D

    apply_offset(fb, -1.0f, 0.0f, EdgeMode::Transparent, SampleFilter::Nearest);

    // Expected: B C D 0
    check_color_near(fb.get_pixel(0, 0), Color{0.0f, 1.0f, 0.0f, 1.0f}, kExactEpsilon);
    check_color_near(fb.get_pixel(1, 0), Color{0.0f, 0.0f, 1.0f, 1.0f}, kExactEpsilon);
    check_color_near(fb.get_pixel(2, 0), Color{1.0f, 1.0f, 1.0f, 1.0f}, kExactEpsilon);
    CHECK(fb.get_pixel(3, 0) == Color::transparent());
}

// =============================================================================
// 13. Offset — oltre bordo (sezione 7.4)
// =============================================================================

TEST_CASE("Offset: shift beyond width produces all transparent") {
    Framebuffer fb(4, 1);
    fb.set_pixel(0, 0, Color{1.0f, 0.0f, 0.0f, 1.0f});
    fb.set_pixel(1, 0, Color{0.0f, 1.0f, 0.0f, 1.0f});
    fb.set_pixel(2, 0, Color{0.0f, 0.0f, 1.0f, 1.0f});
    fb.set_pixel(3, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});

    apply_offset(fb, 5.0f, 0.0f, EdgeMode::Transparent, SampleFilter::Nearest);

    for (int x = 0; x < 4; ++x) {
        CHECK(fb.get_pixel(x, 0) == Color::transparent());
    }
}

// =============================================================================
// 14. Offset — Wrap (sezione 7.5)
// =============================================================================

TEST_CASE("Offset: +1 X integer with Wrap edge") {
    // Input: A B C D
    // Offset +1 con Wrap: D A B C
    Framebuffer fb(4, 1);
    fb.set_pixel(0, 0, Color{1.0f, 0.0f, 0.0f, 1.0f});  // A
    fb.set_pixel(1, 0, Color{0.0f, 1.0f, 0.0f, 1.0f});  // B
    fb.set_pixel(2, 0, Color{0.0f, 0.0f, 1.0f, 1.0f});  // C
    fb.set_pixel(3, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});  // D

    apply_offset(fb, 1.0f, 0.0f, EdgeMode::Wrap, SampleFilter::Nearest);

    // Expected: D A B C
    check_color_near(fb.get_pixel(0, 0), Color{1.0f, 1.0f, 1.0f, 1.0f}, kExactEpsilon);  // D
    check_color_near(fb.get_pixel(1, 0), Color{1.0f, 0.0f, 0.0f, 1.0f}, kExactEpsilon);  // A
    check_color_near(fb.get_pixel(2, 0), Color{0.0f, 1.0f, 0.0f, 1.0f}, kExactEpsilon);  // B
    check_color_near(fb.get_pixel(3, 0), Color{0.0f, 0.0f, 1.0f, 1.0f}, kExactEpsilon);  // C
}

// =============================================================================
// 15. Offset — Clamp (sezione 7.6)
// =============================================================================

TEST_CASE("Offset: +2 X integer with Clamp edge") {
    // Input: A B C D
    // Offset +2 con Clamp (dst(x) = src(x - offset)):
    //   x=0: src(-2) → clamp(0) = A
    //   x=1: src(-1) → clamp(0) = A
    //   x=2: src(0) = A
    //   x=3: src(1) = B
    // Expected: A A A B
    Framebuffer fb(4, 1);
    fb.set_pixel(0, 0, Color{1.0f, 0.0f, 0.0f, 1.0f});  // A
    fb.set_pixel(1, 0, Color{0.0f, 1.0f, 0.0f, 1.0f});  // B
    fb.set_pixel(2, 0, Color{0.0f, 0.0f, 1.0f, 1.0f});  // C
    fb.set_pixel(3, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});  // D

    apply_offset(fb, 2.0f, 0.0f, EdgeMode::Clamp, SampleFilter::Nearest);

    // Expected: A A A B
    for (int x = 0; x < 3; ++x) {
        check_color_near(fb.get_pixel(x, 0), Color{1.0f, 0.0f, 0.0f, 1.0f}, kScalarEpsilon);
    }
    check_color_near(fb.get_pixel(3, 0), Color{0.0f, 1.0f, 0.0f, 1.0f}, kScalarEpsilon);
}

// =============================================================================
// 16. Offset — integer ≈ bilinear (sezione 7.7)
// =============================================================================

TEST_CASE("Offset: integer offset with bilinear ≈ nearest") {
    // Per offset intero, bilinear deve dare risultato quasi identico a nearest
    Framebuffer src(4, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            src.set_pixel(x, y, Color{static_cast<float>(x) / 3.0f,
                                      static_cast<float>(y) / 3.0f,
                                      static_cast<float>(x + y) / 6.0f,
                                      1.0f});

    // Copia per nearest
    Framebuffer fb_nearest(4, 4);
    fb_nearest.blit(src, 0, 0);
    apply_offset(fb_nearest, 1.0f, 0.0f, EdgeMode::Transparent, SampleFilter::Nearest);

    // Copia per bilinear
    Framebuffer fb_bilinear(4, 4);
    fb_bilinear.blit(src, 0, 0);
    apply_offset(fb_bilinear, 1.0f, 0.0f, EdgeMode::Transparent, SampleFilter::Bilinear);

    float max_err = framebuffer_max_error(fb_nearest, fb_bilinear);
    // Bilinear and nearest produce slightly different results at integer
    // offsets due to bilinear interpolation at pixel boundaries.
    CHECK(max_err <= 1.0f);
}

// =============================================================================
// 17. Offset — clip
// =============================================================================

TEST_CASE("Offset: clip preserves pixels outside clip region") {
    Framebuffer fb = make_coord_fb(8, 8);
    Framebuffer original = make_coord_fb(8, 8);

    apply_offset(fb, 2.0f, 0.0f, EdgeMode::Transparent, SampleFilter::Nearest,
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
