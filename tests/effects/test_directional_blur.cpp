// ---------------------------------------------------------------------------
// test_directional_blur.cpp — Directional Blur tests (sezione 8 specifica)
// ---------------------------------------------------------------------------
//
// Copertura:
//   - Identità: length=0, samples=1
//   - Conservazione energia (sum_alpha ≈ 1.0)
//   - Centroide invariato per impulso centrale
//   - Immagine costante con Clamp rimane invariata
//   - Periodicità angolo: 0° ≈ 180°, 90° ≈ 270°
//   - Fast path: generico 0° ≈ orizzontale specializzato
//   - Bounds: margini per angoli e length note
//   - HDR preservato
//   - Simmetria profilo orizzontale

#include <doctest/doctest.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include "src/backends/software/processors/effects/blur/directional_blur.hpp"
#include "tests/effects/test_helpers.hpp"
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::renderer;

// ── Helper: riempi fb con singolo impulso bianco opaco ───────────────────────

namespace {

void fill_impulse(Framebuffer& fb, int cx, int cy) {
    fb.clear(Color::transparent());
    fb.set_pixel(cx, cy, Color{1.0f, 1.0f, 1.0f, 1.0f});
}

} // anonymous namespace

// =============================================================================
// 1. Identity — length = 0 (sezione 8.1)
// =============================================================================

TEST_CASE("DirectionalBlur: length=0 is identity") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_directional_blur(fb, 0.0f, 0.0f);

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

// =============================================================================
// 2. Identity — samples=1 (sezione 8.1)
// =============================================================================

TEST_CASE("DirectionalBlur: samples=1 is identity") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_directional_blur(fb, 45.0f, 10.0f, 1);

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

// =============================================================================
// 3. Conservazione dell'energia — impulso centrale (sezione 8.4)
// =============================================================================

TEST_CASE("DirectionalBlur: energy conservation with central impulse") {
    // Create a 32x32 framebuffer with a single white pixel at (16,16)
    Framebuffer fb(32, 32);
    fb.clear(Color::transparent());
    fb.set_pixel(16, 16, Color{1.0f, 1.0f, 1.0f, 1.0f});

    // Apply a short directional blur (length=4) so it stays away from edges
    apply_directional_blur(fb, 30.0f, 4.0f);

    // Sum of alpha should be approximately 1.0 (energy conserved)
    float total_alpha = sum_alpha(fb);
    CHECK(total_alpha == doctest::Approx(1.0f).epsilon(kBlurEpsilon));
}

// =============================================================================
// 4. Centroide invariato (sezione 8.5)
// =============================================================================

TEST_CASE("DirectionalBlur: centroid unchanged for central impulse") {
    Framebuffer fb(32, 32);
    fb.clear(Color::transparent());
    fb.set_pixel(10, 10, Color{1.0f, 1.0f, 1.0f, 1.0f});

    apply_directional_blur(fb, 45.0f, 4.0f);

    // Compute centroid
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

    CHECK(cx == doctest::Approx(10.0f).epsilon(kBlurEpsilon));
    CHECK(cy == doctest::Approx(10.0f).epsilon(kBlurEpsilon));
}

// =============================================================================
// 5. Immagine costante con Clamp rimane invariata (sezione 8.6)
// =============================================================================

TEST_CASE("DirectionalBlur: constant image stays constant") {
    Framebuffer fb(8, 8);
    const Color constant{0.4f, 0.2f, 0.1f, 0.5f};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            fb.set_pixel(x, y, constant);

    Framebuffer original(8, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            original.set_pixel(x, y, constant);

    // With Clamp edge mode (default in sample_bilinear), a constant image
    // should remain exactly constant after directional blur
    apply_directional_blur(fb, 45.0f, 10.0f);

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            check_color_near(fb.get_pixel(x, y),
                             original.get_pixel(x, y),
                             kScalarEpsilon);
        }
    }
}

// =============================================================================
// 6. Periodicità angolo (sezione 8.7)
// =============================================================================

TEST_CASE("DirectionalBlur: angle 0° ≈ 180°") {
    Framebuffer fb0(16, 16);
    fill_impulse(fb0, 8, 8);
    apply_directional_blur(fb0, 0.0f, 6.0f);

    Framebuffer fb180(16, 16);
    fill_impulse(fb180, 8, 8);
    apply_directional_blur(fb180, 180.0f, 6.0f);

    float max_err = framebuffer_max_error(fb0, fb180);
    CHECK(max_err <= kScalarEpsilon);
}

TEST_CASE("DirectionalBlur: angle 90° ≈ 270°") {
    Framebuffer fb90(16, 16);
    fill_impulse(fb90, 8, 8);
    apply_directional_blur(fb90, 90.0f, 6.0f);

    Framebuffer fb270(16, 16);
    fill_impulse(fb270, 8, 8);
    apply_directional_blur(fb270, 270.0f, 6.0f);

    float max_err = framebuffer_max_error(fb90, fb270);
    CHECK(max_err <= kScalarEpsilon);
}

// =============================================================================
// 7. Fast path: generic 0° ≈ horizontal fast path (sezione 8.8)
// =============================================================================

TEST_CASE("DirectionalBlur: generic ~0° matches horizontal fast path") {
    // Use 0.001° (just above the 0.001 fast-path threshold) to force the
    // generic path, then compare with 0° which uses the horizontal fast path.
    // The 0.001° difference is negligible so results should match closely.
    Framebuffer generic(16, 16);
    fill_impulse(generic, 8, 8);
    apply_directional_blur(generic, 0.001f, 6.0f);  // generic path

    Framebuffer fast(16, 16);
    fill_impulse(fast, 8, 8);
    apply_directional_blur(fast, 0.0f, 6.0f);  // fast path (horizontal)

    float max_err = framebuffer_max_error(generic, fast);
    CHECK(max_err <= kScalarEpsilon);
}

TEST_CASE("DirectionalBlur: generic ~90° matches vertical fast path") {
    // Same idea as above: 90.001° (generic) vs 90° (vertical fast path)
    Framebuffer generic(16, 16);
    fill_impulse(generic, 8, 8);
    apply_directional_blur(generic, 90.001f, 6.0f);  // generic path

    Framebuffer fast(16, 16);
    fill_impulse(fast, 8, 8);
    apply_directional_blur(fast, 90.0f, 6.0f);  // fast path (vertical)

    float max_err = framebuffer_max_error(generic, fast);
    CHECK(max_err <= kScalarEpsilon);
}

// =============================================================================
// 8. Bounds (sezione 8.9)
// =============================================================================

TEST_CASE("DirectionalBlur: bounds margins horizontal") {
    // angle=0°, length=10
    // marginX = ceil(|cos(0)| * 10/2) + 1 = ceil(5) + 1 = 6
    // marginY = ceil(|sin(0)| * 10/2) + 1 = ceil(0) + 1 = 1
    auto [mx, my] = directional_blur_margins(0.0f, 10.0f);
    CHECK(mx == 6);
    CHECK(my == 1);
}

TEST_CASE("DirectionalBlur: bounds margins vertical") {
    // angle=90°, length=10
    // marginX = ceil(|cos(90)| * 5) + 1 = 1
    // marginY = ceil(|sin(90)| * 5) + 1 = 6
    auto [mx, my] = directional_blur_margins(90.0f, 10.0f);
    CHECK(mx == 1);
    CHECK(my == 6);
}

TEST_CASE("DirectionalBlur: bounds margins 45 degrees") {
    // angle=45°, length=10
    // cos(45) = sin(45) ≈ 0.70710678
    // marginX = ceil(0.70710678 * 5) + 1 = ceil(3.5355) + 1 = 4 + 1 = 5
    // marginY = same
    auto [mx, my] = directional_blur_margins(45.0f, 10.0f);
    CHECK(mx == 5);
    CHECK(my == 5);
}

TEST_CASE("DirectionalBlur: bounds margins zero length") {
    auto [mx, my] = directional_blur_margins(45.0f, 0.0f);
    CHECK(mx == 0);
    CHECK(my == 0);
}

// =============================================================================
// 9. HDR: values not clamped after blur
// =============================================================================

TEST_CASE("DirectionalBlur: HDR values preserved") {
    Framebuffer fb(8, 8);
    const Color hdr{2.0f, 0.5f, 4.0f, 1.0f};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            fb.set_pixel(x, y, hdr);

    apply_directional_blur(fb, 0.0f, 4.0f);

    // With constant image, HDR values must survive
    check_color_near(fb.get_pixel(0, 0), hdr, kScalarEpsilon);
}

// =============================================================================
// 10. Simmetria profilo orizzontale
// =============================================================================

TEST_CASE("DirectionalBlur: horizontal blur preserves row energy profile symmetry") {
    // For a horizontal blur (angle=0°), the energy along each row
    // should be symmetric around the impulse centre.
    Framebuffer fb(32, 1);
    fb.clear(Color::transparent());
    fb.set_pixel(16, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});

    apply_directional_blur(fb, 0.0f, 8.0f);

    const Color* row = fb.pixels_row(0);
    for (int d = 1; d <= 4; ++d) {
        CHECK(row[16 + d].r == doctest::Approx(row[16 - d].r).epsilon(kBlurEpsilon));
        CHECK(row[16 + d].g == doctest::Approx(row[16 - d].g).epsilon(kBlurEpsilon));
        CHECK(row[16 + d].b == doctest::Approx(row[16 - d].b).epsilon(kBlurEpsilon));
    }
}
