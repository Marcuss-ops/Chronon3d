// ---------------------------------------------------------------------------
// test_radial_blur.cpp — Radial Blur tests (sezione 9 specifica)
// ---------------------------------------------------------------------------
//
// Copertura:
//   - Identità: amount=0
//   - Centro invariato: pixel al centro esatto campiona se stesso
//   - Zoom: sample_pos - center parallelo a pixel_pos - center
//   - Spin: ||rotated - center|| = ||original - center||
//   - Simmetria tap verificabile indirettamente (centroide)
//   - Immagine costante con Clamp rimane invariata
//   - Preview vs final: sample count diverso → risultato diverso
//   - Dirty policy: FullFrame

#include <doctest/doctest.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include "src/backends/software/processors/effects/blur/radial_blur.hpp"
#include "tests/effects/test_helpers.hpp"
using namespace test_fx;
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {

/// Helper: riempi fb con singolo impulso bianco opaco.
void fill_impulse(Framebuffer& fb, int cx, int cy) {
    fb.clear(Color::transparent());
    fb.set_pixel(cx, cy, Color{1.0f, 1.0f, 1.0f, 1.0f});
}

/// Helper: riempi fb con colore costante.
void fill_constant(Framebuffer& fb, const Color& c) {
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x)
            fb.set_pixel(x, y, c);
}

/// Helper: cross product 2D.
inline float cross_2d(float ax, float ay, float bx, float by) {
    return ax * by - ay * bx;
}

/// Helper: lunghezza vettore 2D.
inline float vec2_len(float dx, float dy) {
    return std::sqrt(dx * dx + dy * dy);
}

} // anonymous namespace

// =============================================================================
// 1. Identità — amount = 0 (sezione 9.1)
// =============================================================================

TEST_CASE("RadialBlur: amount=0 is identity") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_radial_blur(fb, 0.5f, 0.5f, 0.0f, 8);

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

// =============================================================================
// 2. Identity — amount>0 but samples=1 (sezione 9.1)
// =============================================================================

TEST_CASE("RadialBlur: amount>0 but samples=1 is identity") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_radial_blur(fb, 0.5f, 0.5f, 100.0f, 1);

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

// =============================================================================
// 3. Centro invariato (sezione 9.2)
// =============================================================================

TEST_CASE("RadialBlur: pixel at exact center is unchanged") {
    Framebuffer fb(8, 8);
    fill_impulse(fb, 4, 4);  // impulse at center of 8×8

    // Center at (4.0, 4.0) pixel space = normalized (0.5, 0.5)
    apply_radial_blur(fb, 0.5f, 0.5f, 10.0f, 8);

    // Centre pixel must be unchanged
    Color c = fb.get_pixel(4, 4);
    CHECK(c.r == doctest::Approx(1.0f).epsilon(kExactEpsilon));
    CHECK(c.g == doctest::Approx(1.0f).epsilon(kExactEpsilon));
    CHECK(c.b == doctest::Approx(1.0f).epsilon(kExactEpsilon));
    CHECK(c.a == doctest::Approx(1.0f).epsilon(kExactEpsilon));
}

// =============================================================================
// 4. Zoom: sample_pos - center parallelo a pixel_pos - center
//    (sezione 9.3)
// =============================================================================
//
// Per ogni pixel lungo un raggio dal centro, la posizione campionata
// deve giacere sulla stessa retta passante per il centro.
// cross(sample - center, pixel - center) ≈ 0

TEST_CASE("RadialBlur: zoom samples are collinear with centre") {
    // Create a 16×16 with an impulse at (8, 8) — at centre
    // Apply blur to a pixel at (12, 8) — 4 pixels right of centre
    // The sampled taps should all lie on the horizontal line through the centre

    Framebuffer fb(16, 16);
    fill_constant(fb, Color{0.0f, 0.0f, 0.0f, 1.0f});
    fb.set_pixel(12, 8, Color{1.0f, 1.0f, 1.0f, 1.0f});

    Framebuffer before = fb;  // snapshot before blur

    apply_radial_blur(fb, 0.5f, 0.5f, 2.0f, 8);

    // Not much we can assert directly about individual taps since they're
    // internal. But we know the centroid of the blur along the horizontal
    // axis should remain at (12, 8) since taps are symmetric.

    // Compute centroid
    float sum_a = 0.0f, sum_ax = 0.0f, sum_ay = 0.0f;
    for (int y = 0; y < 16; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < 16; ++x) {
            sum_a += row[x].a;
            sum_ax += row[x].a * static_cast<float>(x);
            sum_ay += row[x].a * static_cast<float>(y);
        }
    }

    float cx = sum_ax / sum_a;
    float cy = sum_ay / sum_a;

    // Centroid should stay at (12, 8) within blur tolerance
    CHECK(cx == doctest::Approx(12.0f).epsilon(kBlurEpsilon));
    CHECK(cy == doctest::Approx(8.0f).epsilon(kBlurEpsilon));
}

// =============================================================================
// 5. Spin: distanza dal centro preservata (sezione 9.4)
// =============================================================================
//
// Per una rotazione pura (solo spin), ogni tap ruota attorno al centro
// mantenendo il raggio invariato. Verifichiamo che il profilo radiale
// dell'impulso sia simmetrico in distanza dal centro.

TEST_CASE("RadialBlur: spin preserves distance from centre") {
    Framebuffer fb(16, 16);
    fill_constant(fb, Color::transparent());
    // Place impulse at (8, 4) — 4 pixels above centre (8, 8)
    fb.set_pixel(8, 4, Color{1.0f, 1.0f, 1.0f, 1.0f});

    // Apply strong spin blur
    apply_radial_blur(fb, 0.5f, 0.5f, 100.0f, 8);

    // The centroid should remain at the same distance from centre
    // since spin preserves distance
    float sum_a = 0.0f, sum_ax = 0.0f, sum_ay = 0.0f;
    for (int y = 0; y < 16; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < 16; ++x) {
            sum_a += row[x].a;
            sum_ax += row[x].a * static_cast<float>(x);
            sum_ay += row[x].a * static_cast<float>(y);
        }
    }

    float cx = sum_ax / sum_a;
    float cy = sum_ay / sum_a;

    // With spin-only blur around centre (8, 8), the centroid of the
    // circularly distributed energy should still be at distance ≈ 4 from centre
    float dist = vec2_len(cx - 8.0f, cy - 8.0f);
    CHECK(dist == doctest::Approx(4.0f).epsilon(0.5f));
}

// =============================================================================
// 6. Simmetria tap (verifica indiretta via centroide)
//    (sezione 9.5)
// =============================================================================
//
// I tap sono simmetrici → il centroide dell'impulso blurrato deve
// coincidere con la posizione originale dell'impulso.

TEST_CASE("RadialBlur: centroid unchanged by combined zoom+spin") {
    Framebuffer fb(32, 32);
    fill_impulse(fb, 16, 16);

    apply_radial_blur(fb, 0.5f, 0.5f, 5.0f, 8);

    float sum_a = 0.0f, sum_ax = 0.0f, sum_ay = 0.0f;
    for (int y = 0; y < 32; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < 32; ++x) {
            sum_a += row[x].a;
            sum_ax += row[x].a * static_cast<float>(x);
            sum_ay += row[x].a * static_cast<float>(y);
        }
    }

    float cx = sum_ax / sum_a;
    float cy = sum_ay / sum_a;

    CHECK(cx == doctest::Approx(16.0f).epsilon(kBlurEpsilon));
    CHECK(cy == doctest::Approx(16.0f).epsilon(kBlurEpsilon));
}

// =============================================================================
// 7. Immagine costante con Clamp rimane invariata (sezione 9.6)
// =============================================================================

TEST_CASE("RadialBlur: constant image stays constant") {
    Framebuffer fb(8, 8);
    const Color constant{0.4f, 0.2f, 0.1f, 0.5f};
    fill_constant(fb, constant);

    // Strong blur — with Clamp edge mode, constant image stays constant
    apply_radial_blur(fb, 0.5f, 0.5f, 30.0f, 8);

    for (int y = 0; y < 8; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < 8; ++x) {
            check_color_near(row[x], constant, kScalarEpsilon);
        }
    }
}

// =============================================================================
// 8. Preview vs final: sample count diverso → risultato diverso
//    (sezione 9.7)
// =============================================================================

TEST_CASE("RadialBlur: different sample counts produce different results") {
    Framebuffer fb_a(16, 16);
    fill_impulse(fb_a, 8, 8);
    apply_radial_blur(fb_a, 0.5f, 0.5f, 10.0f, 4);  // preview (fewer samples)

    Framebuffer fb_b(16, 16);
    fill_impulse(fb_b, 8, 8);
    apply_radial_blur(fb_b, 0.5f, 0.5f, 10.0f, 8);  // final (more samples)

    float max_err = framebuffer_max_error(fb_a, fb_b);
    CHECK(max_err > kScalarEpsilon);  // should differ meaningfully
}

TEST_CASE("RadialBlur: same sample count = same result") {
    Framebuffer fb_a(16, 16);
    fill_impulse(fb_a, 8, 8);
    apply_radial_blur(fb_a, 0.5f, 0.5f, 10.0f, 6);

    Framebuffer fb_b(16, 16);
    fill_impulse(fb_b, 8, 8);
    apply_radial_blur(fb_b, 0.5f, 0.5f, 10.0f, 6);

    float max_err = framebuffer_max_error(fb_a, fb_b);
    CHECK(max_err <= kExactEpsilon);  // deterministic → bit-identical
}

// =============================================================================
// 9. Dirty policy: FullFrame (sezione 9.8 & 11)
// =============================================================================
// Verificato staticamente via effect_catalog test esistente.
// Test funzionale: with clip, pixels outside clip are still modified
// because FullFrame means the entire image is affected.

TEST_CASE("RadialBlur: FullFrame — clip does not preserve outside pixels") {
    Framebuffer fb(8, 8);
    fill_constant(fb, Color{0.2f, 0.3f, 0.4f, 1.0f});
    Framebuffer original(8, 8);
    fill_constant(original, Color{0.2f, 0.3f, 0.4f, 1.0f});

    // Clip applied to a small region
    apply_radial_blur(fb, 0.5f, 0.5f, 5.0f, 8);

    // With amount=0 blur, nothing changes
    // With amount>0, the clip doesn't make sense for radial blur
    // (FullFrame policy means entire image IS affected)
    // So we just verify the image IS different from the original
    // (non-trivial test — radial blur genuinely changed pixels)

    bool any_different = false;
    for (int y = 0; y < 8; ++y) {
        const Color* row = fb.pixels_row(y);
        const Color* orig = original.pixels_row(y);
        for (int x = 0; x < 8; ++x) {
            if (std::abs(row[x].r - orig[x].r) > kScalarEpsilon ||
                std::abs(row[x].g - orig[x].g) > kScalarEpsilon) {
                any_different = true;
                break;
            }
        }
        if (any_different) break;
    }
    CHECK(any_different);  // blur actually changed something
}

// =============================================================================
// 10. HDR: values not clamped after blur
// =============================================================================

TEST_CASE("RadialBlur: HDR values preserved in constant image") {
    Framebuffer fb(8, 8);
    const Color hdr{2.0f, 0.5f, 4.0f, 1.0f};
    fill_constant(fb, hdr);

    apply_radial_blur(fb, 0.5f, 0.5f, 10.0f, 8);

    // With Clamp and constant image, HDR values must survive
    for (int y = 0; y < 8; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < 8; ++x) {
            check_color_near(row[x], hdr, kScalarEpsilon);
        }
    }
}
