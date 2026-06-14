// ---------------------------------------------------------------------------
// test_exposure_levels.cpp — Exposure e Levels test (sezione 5 specifica)
// ---------------------------------------------------------------------------
//
// Copertura:
//   - Contratto colore: premultiply/unpremultiply roundtrip
//   - Exposure identità (stops = 0)
//   - Exposure +1 stop con half_alpha premul → (0.8, 0.4, 0.2, 0.5)
//   - Exposure -1 stop con opaco → (0.4, 0.2, 0.1, 1.0)
//   - Exposure HDR: {2.0, 0.5, 4.0, 1.0} +1stop → {4.0, 1.0, 8.0, 1.0}
//   - Alpha invariata (esatta) per Exposure
//   - Levels identità: valori -0.5..2.0 invariati
//   - Levels black/white con estrapolazione HDR
//   - Levels gamma: sqrt(0.5) ≈ 0.70710678
//   - Canali separati: solo red modificato
//   - Clip parziale: pixel fuori dal clip bit-identici

#include <doctest/doctest.h>
#include <chronon3d/effects/color_contract.hpp>
#include "src/backends/software/processors/effects/color/exposure_levels.hpp"
#include "tests/effects/test_helpers.hpp"
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::renderer;

// =============================================================================
// 1. Contratto colore — unpremultiply / premultiply (sezione 5.1)
// =============================================================================

TEST_CASE("ColorContract: unpremultiply + premultiply roundtrip") {
    // Input premoltiplicato: RGB = 0.8*0.5=0.4, 0.4*0.5=0.2, 0.2*0.5=0.1
    Color premul = kHalfAlphaPremul;  // {0.4, 0.2, 0.1, 0.5}

    auto straight = color::unpremultiply_rgb(premul);

    CHECK(straight.r == doctest::Approx(kHalfStraightR));
    CHECK(straight.g == doctest::Approx(kHalfStraightG));
    CHECK(straight.b == doctest::Approx(kHalfStraightB));

    Color roundtrip = color::premultiply_rgb(straight, premul.a);
    check_color_near(roundtrip, premul, kScalarEpsilon);
}

TEST_CASE("ColorContract: alpha zero returns transparent") {
    auto straight = color::unpremultiply_rgb(Color::transparent());
    CHECK(straight.r == doctest::Approx(0.0f));
    CHECK(straight.g == doctest::Approx(0.0f));
    CHECK(straight.b == doctest::Approx(0.0f));

    Color result = color::premultiply_rgb({0.5f, 0.5f, 0.5f}, 0.0f);
    CHECK(result == Color::transparent());
}

TEST_CASE("ColorContract: HDR values survive unpromultiply roundtrip") {
    // HDR premoltiplicato
    auto straight = color::unpremultiply_rgb(kHdrPremul);
    CHECK(straight.r == doctest::Approx(2.0f));
    CHECK(straight.g == doctest::Approx(0.5f));
    CHECK(straight.b == doctest::Approx(4.0f));

    Color roundtrip = color::premultiply_rgb(straight, 1.0f);
    check_color_near(roundtrip, kHdrPremul, kExactEpsilon);
}

// =============================================================================
// 2. Exposure — identità (sezione 5.2)
// =============================================================================

TEST_CASE("Exposure: stops=0 is identity") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_exposure(fb, 0.0f);

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

// =============================================================================
// 3. Exposure — +1 stop (sezione 5.3)
// =============================================================================

TEST_CASE("Exposure: +1 stop with half-alpha premul") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHalfAlphaPremul);  // {0.4, 0.2, 0.1, 0.5}

    apply_exposure(fb, 1.0f);

    // Straight: 0.8→1.6, 0.4→0.8, 0.2→0.4
    // Premultiplied: 1.6*0.5=0.8, 0.8*0.5=0.4, 0.4*0.5=0.2
    Color expected{0.8f, 0.4f, 0.2f, 0.5f};
    Color actual = fb.get_pixel(0, 0);
    check_color_near(actual, expected, kScalarEpsilon);

    // Alpha must be preserved exactly
    CHECK(actual.a == doctest::Approx(0.5f));
}

// =============================================================================
// 4. Exposure — -1 stop (sezione 5.4)
// =============================================================================

TEST_CASE("Exposure: -1 stop with opaque") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kOpaquePremul);  // {0.8, 0.4, 0.2, 1.0}

    apply_exposure(fb, -1.0f);  // 2^-1 = 0.5

    // Expected: {0.4, 0.2, 0.1, 1.0}
    Color expected{0.4f, 0.2f, 0.1f, 1.0f};
    check_color_near(fb.get_pixel(0, 0), expected, kScalarEpsilon);
}

// =============================================================================
// 5. Exposure — HDR (sezione 5.5)
// =============================================================================

TEST_CASE("Exposure: HDR values no accidental clamp") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kHdrPremul);  // {2.0, 0.5, 4.0, 1.0}

    apply_exposure(fb, 1.0f);

    // {2.0*2=4.0, 0.5*2=1.0, 4.0*2=8.0, 1.0}
    Color expected{4.0f, 1.0f, 8.0f, 1.0f};
    check_color_near(fb.get_pixel(0, 0), expected, kScalarEpsilon);
}

// =============================================================================
// 6. Exposure — alpha preservata (sezione 5.6)
// =============================================================================

TEST_CASE("Exposure: alpha preserved for various inputs") {
    auto test_alpha = [](Color input, float stops) {
        Framebuffer fb(1, 1);
        fb.set_pixel(0, 0, input);
        apply_exposure(fb, stops);
        Color output = fb.get_pixel(0, 0);
        CHECK(output.a == doctest::Approx(input.a).epsilon(kExactEpsilon));
        CHECK(std::isfinite(output.r));
        CHECK(std::isfinite(output.g));
        CHECK(std::isfinite(output.b));
    };

    test_alpha(kHalfAlphaPremul, 2.0f);
    test_alpha(kOpaquePremul, -2.0f);
    test_alpha(kHdrPremul, 0.5f);
    test_alpha(kHdrHalfPremul, -1.0f);
    test_alpha(kNegativePremul, 1.0f);
}

TEST_CASE("Levels: alpha preserved for various inputs") {
    auto test_alpha = [](Color input) {
        Framebuffer fb(1, 1);
        fb.set_pixel(0, 0, input);
        apply_levels(fb,
                     0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                     0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                     0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                     0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        Color output = fb.get_pixel(0, 0);
        CHECK(output.a == doctest::Approx(input.a).epsilon(kExactEpsilon));
    };

    test_alpha(kHalfAlphaPremul);   // alpha 0.5
    test_alpha(kHdrHalfPremul);     // alpha 0.5 HDR
    test_alpha(kOpaquePremul);      // alpha 1.0
    test_alpha(kTransparent);       // alpha 0.0
}

TEST_CASE("Exposure: transparent input stays transparent") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color::transparent());
    apply_exposure(fb, 5.0f);
    Color output = fb.get_pixel(0, 0);
    CHECK(output == Color::transparent());
}

TEST_CASE("Exposure: negative RGB values preserved") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, kNegativePremul);  // {-0.2, 0.3, 1.5, 1.0}
    apply_exposure(fb, 1.0f);
    // Expected: {-0.4, 0.6, 3.0, 1.0}
    Color expected{-0.4f, 0.6f, 3.0f, 1.0f};
    check_color_near(fb.get_pixel(0, 0), expected, kScalarEpsilon);
}

// =============================================================================
// 7. Levels — identità (sezione 5.7)
// =============================================================================

TEST_CASE("Levels: identity preserves values -0.5 to 2.0") {
    // Levels identity: in_black=0, in_white=1, gamma=1, out_black=0, out_white=1
    const float inputs[] = {-0.5f, 0.0f, 0.25f, 0.5f, 1.0f, 2.0f};
    Framebuffer fb(6, 1);
    for (int x = 0; x < 6; ++x)
        fb.set_pixel(x, 0, Color{inputs[x], inputs[x], inputs[x], 1.0f});

    // identity params on all channels + master
    apply_levels(fb,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,    // master
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,    // red
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,    // green
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);   // blue

    for (int x = 0; x < 6; ++x) {
        Color c = fb.get_pixel(x, 0);
        CHECK(c.r == doctest::Approx(inputs[x]).epsilon(kScalarEpsilon));
        CHECK(c.g == doctest::Approx(inputs[x]).epsilon(kScalarEpsilon));
        CHECK(c.b == doctest::Approx(inputs[x]).epsilon(kScalarEpsilon));
    }
}

// =============================================================================
// 8. Levels — black/white adjustment (sezione 5.8)
// =============================================================================

TEST_CASE("Levels: black/white mapping with HDR extrapolation") {
    // input_black=0.25, input_white=0.75, gamma=1
    // output_black=0, output_white=1
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

    // 0.25 → (0.25-0.25)/(0.75-0.25) = 0.0 → output 0.0
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(0.0f).epsilon(kScalarEpsilon));
    // 0.50 → (0.50-0.25)/(0.50) = 0.5 → output 0.5
    CHECK(fb.get_pixel(1, 0).r == doctest::Approx(0.5f).epsilon(kScalarEpsilon));
    // 0.75 → (0.75-0.25)/(0.50) = 1.0 → output 1.0
    CHECK(fb.get_pixel(2, 0).r == doctest::Approx(1.0f).epsilon(kScalarEpsilon));
    // 1.00 → (1.00-0.25)/(0.50) = 1.5 → output 1.5 (HDR extrapolation!)
    CHECK(fb.get_pixel(3, 0).r == doctest::Approx(1.5f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 9. Levels — gamma (sezione 5.9)
// =============================================================================

TEST_CASE("Levels: gamma = 2, input 0.5 applies sqrt") {
    Framebuffer fb(1, 1);
    fb.set_pixel(0, 0, Color{0.5f, 0.5f, 0.5f, 1.0f});

    apply_levels(fb,
                 0.0f, 1.0f, 2.0f, 0.0f, 1.0f,  // master gamma = 2
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,    // red identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,    // green identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);   // blue identity

    // pow(0.5, 1/2) = sqrt(0.5) ≈ 0.70710678
    float expected = std::sqrt(0.5f);
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(expected).epsilon(kScalarEpsilon));
}

// =============================================================================
// 10. Levels — canali separati (sezione 5.10)
// =============================================================================

TEST_CASE("Levels: only red channel modified") {
    Framebuffer fb(1, 1);
    // Input: R=0.2, G=0.4, B=0.6  (straight, opaco)
    fb.set_pixel(0, 0, Color{0.2f, 0.4f, 0.6f, 1.0f});

    // Only red: in_black=0, in_white=0.5 → doubling the red channel
    apply_levels(fb,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,    // master identity
                 0.0f, 0.5f, 1.0f, 0.0f, 1.0f,    // red: in_white=0.5 → R=0.4
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,    // green identity
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);   // blue identity

    Color output = fb.get_pixel(0, 0);
    // R = 0.2 / 0.5 = 0.4
    CHECK(output.r == doctest::Approx(0.4f).epsilon(kScalarEpsilon));
    // G e B unchanged
    CHECK(output.g == doctest::Approx(0.4f).epsilon(kScalarEpsilon));
    CHECK(output.b == doctest::Approx(0.6f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 11. Clip test — Exposure e Levels (sezione 5.11)
// =============================================================================

TEST_CASE("Exposure: clip preserves pixels outside clip region") {
    Framebuffer fb = ::make_coord_fb(8, 8);
    Framebuffer original = ::make_coord_fb(8, 8);

    // Applica exposure su tutto il fb
    apply_exposure(fb, 2.0f);
    // Ora riapplica con clip su una porzione, su una copia
    Framebuffer fb2 = ::make_coord_fb(8, 8);
    apply_exposure(fb2, 2.0f,
                   raster::BBox{2, 3, 6, 7});

    // Pixel fuori dal clip devono essere identici a quelli senza clip
    // (entrambi sono stati esposti, ma il clip non cambia il risultato
    //  perché Exposure è per-pixel — testiamo che fuori dal clip siano invariati)
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            bool inside = (x >= 2 && x < 6 && y >= 3 && y < 7);
            if (!inside) {
                // Fuori dal clip: deve essere identico all'originale
                ::check_color_near(fb2.get_pixel(x, y),
                                 original.get_pixel(x, y),
                                 kExactEpsilon);
            }
        }
    }
}

TEST_CASE("Levels: clip preserves pixels outside clip region") {
    Framebuffer fb = ::make_coord_fb(8, 8);
    Framebuffer fb2 = ::make_coord_fb(8, 8);

    // Applica levels con clip parziale
    apply_levels(fb2,
                 0.0f, 0.5f, 1.0f, 0.0f, 1.0f,  // master: raddoppia
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                 raster::BBox{2, 3, 6, 7});

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            bool inside = (x >= 2 && x < 6 && y >= 3 && y < 7);
            if (!inside) {
                ::check_color_near(fb2.get_pixel(x, y),
                                 fb.get_pixel(x, y),
                                 kExactEpsilon);
            }
        }
    }
}
