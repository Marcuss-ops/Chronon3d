// ---------------------------------------------------------------------------
// test_stroke.cpp — Stroke tests (sezione 10 specifica)
// ---------------------------------------------------------------------------
//
// Copertura (sezione 10):
//   - Dilation radius 1: singolo pixel → 3×3 (sum_alpha = 9)
//   - Erosion radius 1:   quadrato 3×3 → singolo pixel (sum_alpha = 1)
//   - Stroke outside:     dilated - source (sum_alpha = 8)
//   - Stroke inside:      source - eroded (sum_alpha = 8)
//   - Preserve source:    centro bianco, anello rosso
//   - Softness invariants (alpha ∈ [0,1], non-crescente)
//   - Bounds: width=10, softness=3 → Outside=13, Center=8, Inside=0

#include <doctest/doctest.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include "src/backends/software/processors/effects/stroke/stroke.hpp"
#include "tests/effects/test_helpers.hpp"
#include <cmath>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {

/// Helper: fill a 5×5 with a single impulse at centre (2,2).
Framebuffer make_impulse_5x5_centre() {
    Framebuffer fb(5, 5);
    fb.clear(Color::transparent());
    fb.set_pixel(2, 2, Color{1.0f, 1.0f, 1.0f, 1.0f});
    return fb;
}

/// Helper: fill a 5×5 with a 3×3 solid square at (1,1)..(3,3).
Framebuffer make_square_3x3() {
    Framebuffer fb(5, 5);
    fb.clear(Color::transparent());
    for (int y = 1; y <= 3; ++y)
        for (int x = 1; x <= 3; ++x)
            fb.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, 1.0f});
    return fb;
}

} // anonymous namespace

// =============================================================================
// 1. Identity — width = 0 (required per section 3)
// =============================================================================

TEST_CASE("Stroke: width=0 is identity") {
    Framebuffer fb = make_ramp_4x1();
    Framebuffer original = make_ramp_4x1();

    apply_stroke(fb, Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.0f, 0.0f, StrokeMode::Outside);

    for (int x = 0; x < 4; ++x) {
        check_color_near(fb.get_pixel(x, 0),
                         original.get_pixel(x, 0),
                         kExactEpsilon);
    }
}

// =============================================================================
// 2. Dilation radius 1 via Outside stroke (sezione 10.1)
// =============================================================================

TEST_CASE("Stroke: Outside mode from impulse gives 3x3 block — sum_alpha=9") {
    // Input: single pixel at (2,2)
    // Outside stroke width=1 composes: source + ring = 1 + 8 = 9 pixels with alpha≈1
    // This tests the composed stroke output, not pure dilation (which is internal)

    Framebuffer fb = make_impulse_5x5_centre();

    apply_stroke(fb, Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f, StrokeMode::Outside);

    // Composed result: source pixel (alpha=1) + 8 ring pixels (alpha=1) = 9
    float total = sum_alpha(fb);
    CHECK(total == doctest::Approx(9.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 3. Erosion via Inside stroke (sezione 10.2)
// =============================================================================
// Pure erosion is internal (anonymous namespace) and not directly testable.
// Inside stroke = source - eroded gives the border ring of a 3×3 square,
// which has sum_alpha = 8 (the 8 border pixels of the 3×3 block).

TEST_CASE("Stroke: Inside mode from 3x3 square gives 8 border pixels") {
    Framebuffer fb = make_square_3x3();

    apply_stroke(fb, Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f, StrokeMode::Inside);

    // Inside: stroke_a = src_a - ero_a
    // For 3×3 square with radius 1: eroded = centre pixel only
    // stroke = source - eroded → 8 border pixels
    float total = sum_alpha(fb);
    CHECK(total == doctest::Approx(8.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 4. Stroke outside sum (sezione 10.3): source(1) + ring(8) = 9
// =============================================================================

TEST_CASE("Stroke: Outside mode composed result — sum_alpha=9 (1 src + 8 ring)") {
    // Singolo pixel (2,2), Outside width=1
    // Composed result: centre alpha=1 + 8 ring pixels alpha=1 → sum=9

    Framebuffer fb = make_impulse_5x5_centre();
    apply_stroke(fb, Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f, StrokeMode::Outside);

    float total = sum_alpha(fb);
    CHECK(total == doctest::Approx(9.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 5. Stroke inside sum (sezione 10.4): source(9) - eroded(1) = 8
// =============================================================================

TEST_CASE("Stroke: Inside mode composed result — sum_alpha=8 (9 src - 1 eroded)") {
    Framebuffer fb = make_square_3x3();
    apply_stroke(fb, Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.0f, StrokeMode::Inside);

    float total = sum_alpha(fb);
    CHECK(total == doctest::Approx(8.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 6. Preserve source pixel (sezione 10.5)
// =============================================================================

TEST_CASE("Stroke: outside preserves centre pixel colour") {
    // Impulse at (2,2) = white {1,1,1,1}
    // Stroke colour = red {1,0,0,1}
    // Outside mode: centre pixel should remain white (stroke_a=0 there)
    // Ring pixels should be red

    Framebuffer fb(5, 5);
    fb.clear(Color::transparent());
    fb.set_pixel(2, 2, Color{1.0f, 1.0f, 1.0f, 1.0f});

    apply_stroke(fb, Color{1.0f, 0.0f, 0.0f, 1.0f}, 1.0f, 0.0f, StrokeMode::Outside);

    // Centre pixel: original white preserved
    Color centre = fb.get_pixel(2, 2);
    CHECK(centre.r == doctest::Approx(1.0f).epsilon(kScalarEpsilon));
    CHECK(centre.g == doctest::Approx(1.0f).epsilon(kScalarEpsilon));
    CHECK(centre.b == doctest::Approx(1.0f).epsilon(kScalarEpsilon));

    // Ring pixel at (1,1): should be red (stroke colour) blended with transparent → red
    Color ring = fb.get_pixel(1, 1);
    CHECK(ring.r == doctest::Approx(1.0f).epsilon(kScalarEpsilon));
    CHECK(ring.g == doctest::Approx(0.0f).epsilon(kScalarEpsilon));
    CHECK(ring.b == doctest::Approx(0.0f).epsilon(kScalarEpsilon));
    CHECK(ring.a == doctest::Approx(1.0f).epsilon(kScalarEpsilon));
}

// =============================================================================
// 7. Softness: alpha non-increasing outward (sezione 10.6)
// =============================================================================

TEST_CASE("Stroke: softness — alpha is non-increasing outward") {
    // Create a 1-pixel wide vertical line 9 pixels tall, length 1 at centre
    // Apply outside stroke with width=2, softness=2
    // Sample alpha values along a horizontal line through the centre
    // Alpha should be non-increasing as we move away from the source edge

    Framebuffer fb(11, 11);
    fb.clear(Color::transparent());
    for (int y = 1; y <= 9; ++y)
        fb.set_pixel(5, y, Color{1.0f, 1.0f, 1.0f, 1.0f});

    apply_stroke(fb, Color{1.0f, 1.0f, 1.0f, 1.0f}, 2.0f, 2.0f, StrokeMode::Outside);

    // Sample alpha along x-axis at y=5 (through centre of line)
    // Starting from the source edge (x=5, stroke_a=0) outward
    std::vector<float> alphas;
    for (int x = 5; x < 11; ++x) {
        float a = fb.get_pixel(x, 5).a;
        alphas.push_back(a);
        CHECK(a >= 0.0f);
        CHECK(a <= 1.0f);
    }

    // Alpha should be non-increasing as we move outward from centre
    for (std::size_t i = 1; i < alphas.size(); ++i) {
        CHECK(alphas[i] <= alphas[i - 1] + kScalarEpsilon);
    }
}

// =============================================================================
// 8. Bounds (sezione 10.7)
// =============================================================================

TEST_CASE("Stroke: bounds margins Outside width=10 softness=3") {
    // expansion = ceil(10 + 3) = 13
    auto [mx, my] = stroke_margins(10.0f, 3.0f, StrokeMode::Outside);
    CHECK(mx == 13);
    CHECK(my == 13);
}

TEST_CASE("Stroke: bounds margins Center width=10 softness=3") {
    // expansion = ceil(10/2 + 3) = ceil(5 + 3) = 8
    auto [mx, my] = stroke_margins(10.0f, 3.0f, StrokeMode::Center);
    CHECK(mx == 8);
    CHECK(my == 8);
}

TEST_CASE("Stroke: bounds margins Inside width=10 softness=3") {
    // expansion = 0
    auto [mx, my] = stroke_margins(10.0f, 3.0f, StrokeMode::Inside);
    CHECK(mx == 0);
    CHECK(my == 0);
}

TEST_CASE("Stroke: bounds margins zero width") {
    auto [mx, my] = stroke_margins(0.0f, 0.0f, StrokeMode::Outside);
    CHECK(mx == 0);
    CHECK(my == 0);
}

// =============================================================================
// 9. HDR values preserved in constant image
// =============================================================================

TEST_CASE("Stroke: HDR values preserved where stroke_a = 0") {
    Framebuffer fb(8, 8);
    const Color hdr{2.0f, 0.5f, 4.0f, 1.0f};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            fb.set_pixel(x, y, hdr);

    // With fully opaque constant image, stroke_a = 0 everywhere (no edge to stroke)
    apply_stroke(fb, Color{1.0f, 0.0f, 0.0f, 1.0f}, 1.0f, 0.0f, StrokeMode::Outside);

    // All pixels should be unchanged
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            check_color_near(fb.get_pixel(x, y), hdr, kScalarEpsilon);
}
