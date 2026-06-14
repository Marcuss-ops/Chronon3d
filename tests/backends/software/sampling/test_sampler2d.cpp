// ---------------------------------------------------------------------------
// test_sampler2d.cpp — Sampler2D tests (sezione 4 della specifica)
// ---------------------------------------------------------------------------
//
// Copertura:
//   - EdgeMode con dimensione 4 per Clamp, Wrap, Mirror, Transparent
//   - Nearest su texture 2×2 ai centri esatti (A=0.5,0.5 B=1.5,0.5 ...)
//   - Bilinear al centro dei 4 pixel: (0+1+2+3)/4 = 1.5
//   - Bilinear al centro esatto del primo pixel = 0.0
//   - Compatibilità con Framebuffer::sample()

#include <doctest/doctest.h>
#include <chronon3d/backends/software/sampling/sampler2d.hpp>
#include <chronon3d/backends/software/sampling/edge_mode.hpp>
#include "tests/effects/test_helpers.hpp"
using namespace test_fx;
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::sampling;

// =============================================================================
// EdgeMode con dimensione 4
// =============================================================================

TEST_CASE("Sampler2D: Clamp edge con size=4") {
    // indice:  -2 -1  0  1  2  3  4  5
    // atteso:   0  0  0  1  2  3  3  3
    CHECK(clamp_coord(-2, 4) == 0);
    CHECK(clamp_coord(-1, 4) == 0);
    CHECK(clamp_coord( 0, 4) == 0);
    CHECK(clamp_coord( 1, 4) == 1);
    CHECK(clamp_coord( 2, 4) == 2);
    CHECK(clamp_coord( 3, 4) == 3);
    CHECK(clamp_coord( 4, 4) == 3);
    CHECK(clamp_coord( 5, 4) == 3);
}

TEST_CASE("Sampler2D: Wrap edge con size=4") {
    // indice:  -2 -1  0  1  2  3  4  5
    // atteso:   2  3  0  1  2  3  0  1
    CHECK(wrap_coord(-2, 4) == 2);
    CHECK(wrap_coord(-1, 4) == 3);
    CHECK(wrap_coord( 0, 4) == 0);
    CHECK(wrap_coord( 1, 4) == 1);
    CHECK(wrap_coord( 2, 4) == 2);
    CHECK(wrap_coord( 3, 4) == 3);
    CHECK(wrap_coord( 4, 4) == 0);
    CHECK(wrap_coord( 5, 4) == 1);
}

TEST_CASE("Sampler2D: Mirror edge con size=4, periodo=6") {
    // periodo = 2N-2 = 6
    // indice:  -2 -1  0  1  2  3  4  5  6
    // atteso:   2  1  0  1  2  3  2  1  0
    CHECK(mirror_coord(-2, 4) == 2);
    CHECK(mirror_coord(-1, 4) == 1);
    CHECK(mirror_coord( 0, 4) == 0);
    CHECK(mirror_coord( 1, 4) == 1);
    CHECK(mirror_coord( 2, 4) == 2);
    CHECK(mirror_coord( 3, 4) == 3);
    CHECK(mirror_coord( 4, 4) == 2);
    CHECK(mirror_coord( 5, 4) == 1);
    CHECK(mirror_coord( 6, 4) == 0);
}

TEST_CASE("Sampler2D: Transparent edge fuori [0, N-1]") {
    Framebuffer fb(4, 4);
    fb.clear(Color{0.5f, 0.5f, 0.5f, 1.0f});
    Sampler2D sampler(fb, EdgeMode::Transparent);

    Color c = sampler.nearest(-1.0f, 0.0f);
    CHECK(c == Color::transparent());

    c = sampler.nearest(4.0f, 0.0f);
    CHECK(c == Color::transparent());

    c = sampler.nearest(0.0f, -1.0f);
    CHECK(c == Color::transparent());

    c = sampler.nearest(0.0f, 4.0f);
    CHECK(c == Color::transparent());
}

// =============================================================================
// Nearest su 2×2 con valori rossi
// =============================================================================

TEST_CASE("Sampler2D: nearest 2×2 ai centri esatti") {
    Framebuffer fb = make_red_2x2();
    Sampler2D sampler(fb, EdgeMode::Clamp);

    // Centri: A(0.5,0.5)=0, B(1.5,0.5)=1, C(0.5,1.5)=2, D(1.5,1.5)=3
    CHECK(sampler.nearest(0.5f, 0.5f).r == doctest::Approx(0.0f));
    CHECK(sampler.nearest(1.5f, 0.5f).r == doctest::Approx(1.0f));
    CHECK(sampler.nearest(0.5f, 1.5f).r == doctest::Approx(2.0f));
    CHECK(sampler.nearest(1.5f, 1.5f).r == doctest::Approx(3.0f));
}

// =============================================================================
// Bilinear
// =============================================================================

TEST_CASE("Sampler2D: bilinear 2×2 al centro dei 4 pixel = 1.5") {
    Framebuffer fb = make_red_2x2();
    Sampler2D sampler(fb, EdgeMode::Clamp);

    // Al centro dei quattro pixel: (0 + 1 + 2 + 3) / 4 = 1.5
    Color c = sampler.bilinear(1.0f, 1.0f);
    check_color_near(c, Color{1.5f, 0.0f, 0.0f, 1.0f}, kExactEpsilon);
}

TEST_CASE("Sampler2D: bilinear al centro esatto del primo pixel = 0.0") {
    Framebuffer fb = make_red_2x2();
    Sampler2D sampler(fb, EdgeMode::Clamp);

    // Centro esatto del primo pixel (0,0) a (0.5, 0.5)
    CHECK(sampler.bilinear(0.5f, 0.5f).r == doctest::Approx(0.0f));
}

TEST_CASE("Sampler2D: bilinear matches Framebuffer::sample_bilinear") {
    Framebuffer fb(4, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            fb.set_pixel(x, y, Color{static_cast<float>(x) * 0.25f,
                                      static_cast<float>(y) * 0.25f,
                                      static_cast<float>(x + y) * 0.125f,
                                      1.0f});

    Sampler2D sampler(fb, EdgeMode::Clamp);

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float fx = static_cast<float>(x) + 0.5f;
            const float fy = static_cast<float>(y) + 0.5f;
            Color expected = fb.sample_bilinear(fx, fy);
            Color actual = sampler.bilinear(fx, fy);
            check_color_near(actual, expected, kExactEpsilon);
        }
    }
}
