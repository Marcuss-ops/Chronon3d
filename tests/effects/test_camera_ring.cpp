// ---------------------------------------------------------------------------
// test_camera_ring.cpp — Camera Ring / Petzval field-curvature diagnostics.
//
// Five focused groups mirror the optical acceptance suite:
//   1. CoC geometry: elliptical safe zone, symmetry, smooth monotonic growth,
//      full saturation at the corners.
//   2. Nyquist collapse: single-pixel checker survives at the optical axis and
//      collapses to flat grey at the lateral extremities.
//   3. Typographic three-zone check: sharp central edge (<= 2 px), defocused
//      lateral edge (>= 6 px at this fixture scale) and luminous-energy
//      conservation within 1.5%.
//   4. Chromatic isolation: zero R/B separation on the optical axis, visible
//      radial dispersion at the lateral extremities.
//   5. Dynamic rig coupling + deterministic CPU performance proxy.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/camera_ring.hpp>
#include <chronon3d/math/color.hpp>
#include "src/backends/software/processors/effects/blur/camera_ring_processor.hpp"
#include "tests/effects/test_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace chronon3d;

namespace {

Framebuffer make_image(int width, int height, const Color& background) {
    Framebuffer fb(width, height);
    fb.clear(background);
    return fb;
}

void fill_rect(Framebuffer& fb, int x0, int y0, int x1, int y1, const Color& color) {
    for (int y = std::max(0, y0); y < std::min(fb.height(), y1); ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = std::max(0, x0); x < std::min(fb.width(), x1); ++x)
            row[x] = color;
    }
}

float luma(const Color& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

double total_luma(const Framebuffer& fb) {
    double sum = 0.0;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) sum += static_cast<double>(luma(row[x]));
    }
    return sum;
}

// Width of a dark-to-bright edge, from 10% to 90% of the local bar luminance.
int edge_width(const Framebuffer& fb, int y, int scan_x0, int scan_x1,
               float bar_luma) {
    const float low = bar_luma * 0.1f;
    const float high = bar_luma * 0.9f;
    int first_high = -1;
    int last_low = -1;
    for (int x = scan_x0; x < scan_x1; ++x) {
        const float value = luma(fb.get_pixel(x, y));
        if (first_high >= 0) break;
        if (value <= low) {
            last_low = x;
        } else if (value >= high && last_low >= 0) {
            first_high = x;
        }
    }
    if (first_high < 0 || last_low < 0 || first_high <= last_low) return -1;
    return first_high - last_low;
}

float max_chroma_error(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    float maximum = 0.0f;
    for (int y = std::max(0, y0); y < std::min(fb.height(), y1); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = std::max(0, x0); x < std::min(fb.width(), x1); ++x)
            maximum = std::max(maximum, std::abs(row[x].r - row[x].b));
    }
    return maximum;
}

float average_horizontal_gradient(const Framebuffer& fb, int x0, int x1, int y) {
    float sum = 0.0f;
    int count = 0;
    for (int x = std::max(0, x0); x + 1 < std::min(fb.width(), x1); ++x) {
        sum += std::abs(luma(fb.get_pixel(x + 1, y)) - luma(fb.get_pixel(x, y)));
        ++count;
    }
    return count > 0 ? sum / static_cast<float>(count) : 0.0f;
}

CameraRingParams fixture_params() {
    CameraRingParams p;
    p.center = {0.5f, 0.5f};
    p.axis_scale = {0.68f, 0.32f};
    p.inner_radius = 0.22f;
    p.outer_radius = 0.78f;
    p.max_blur_radius = 0.03f;
    p.chroma_dispersion = 0.0f;
    p.anamorphic_stretch = 1.0f;
    p.samples = 16;
    return p;
}

}  // namespace

// ===========================================================================
// Test 1 — CoC heat-map geometry
// ===========================================================================

TEST_CASE("CameraRing: CoC map is a smooth symmetric elliptical corridor") {
    const CameraRingParams p{};
    using effects::camera_ring_coc;

    CHECK(camera_ring_coc(p, p.center.x, p.center.y) == doctest::Approx(0.0f));
    CHECK(camera_ring_coc(p, p.center.x + p.axis_scale.x * p.inner_radius,
                          p.center.y) == doctest::Approx(0.0f));
    CHECK(camera_ring_coc(p, p.center.x + p.axis_scale.x * p.outer_radius,
                          p.center.y) == doctest::Approx(1.0f));
    CHECK(camera_ring_coc(p, 0.0f, 0.0f) == doctest::Approx(1.0f));
    CHECK(camera_ring_coc(p, 1.0f, 1.0f) == doctest::Approx(1.0f));

    // Mirror symmetry around the optical center on both axes.
    for (int i = 1; i <= 20; ++i) {
        const float d = 0.01f * static_cast<float>(i);
        CHECK(camera_ring_coc(p, 0.5f + d, 0.5f) ==
              doctest::Approx(camera_ring_coc(p, 0.5f - d, 0.5f)));
        CHECK(camera_ring_coc(p, 0.5f, 0.5f + d) ==
              doctest::Approx(camera_ring_coc(p, 0.5f, 0.5f - d)));
    }

    // Monotonic, C1 smoothstep growth along the horizontal axis.
    float previous = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        const float radius = p.outer_radius * static_cast<float>(i) / 100.0f;
        const float value = camera_ring_coc(p, 0.5f + p.axis_scale.x * radius, 0.5f);
        CHECK(value >= previous - 1.0e-6f);
        previous = value;
    }

    // The 0.5 iso-contour is an ellipse whose horizontal/vertical ratio is the
    // authored axis-scale ratio: a horizontal text corridor, not a circle.
    const float mid = 0.5f * (p.inner_radius + p.outer_radius);
    const float horizontal = p.axis_scale.x * mid;
    const float vertical = p.axis_scale.y * mid;
    CHECK(camera_ring_coc(p, 0.5f + horizontal, 0.5f) ==
          doctest::Approx(0.5f).epsilon(1.0e-4));
    CHECK(camera_ring_coc(p, 0.5f, 0.5f + vertical) ==
          doctest::Approx(0.5f).epsilon(1.0e-4));
    CHECK(horizontal / vertical ==
          doctest::Approx(p.axis_scale.x / p.axis_scale.y));
}

// ===========================================================================
// Test 2 — Nyquist grid / spatial-frequency collapse
// ===========================================================================

TEST_CASE("CameraRing: single-pixel Nyquist grid stays sharp at center and "
          "collapses to grey at the lateral edges") {
    constexpr int kWidth = 320;
    constexpr int kHeight = 180;
    Framebuffer fb(kWidth, kHeight);
    for (int y = 0; y < kHeight; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = 0; x < kWidth; ++x) {
            const float v = (x % 2 == 0) ? 1.0f : 0.0f;
            row[x] = Color{v, v, v, 1.0f};
        }
    }
    const Framebuffer original = fb;
    CameraRingParams p = fixture_params();
    // A large radius makes the 320 px fixture's lateral checker collapse
    // unambiguously while the central safe zone remains an exact copy.
    p.max_blur_radius = 0.15f;

    renderer::apply_camera_ring(fb, p);

    // The optical axis is inside the exact-copy safe zone.
    for (int x = 150; x < 170; ++x) {
        for (int y = 85; y < 95; ++y) {
            const Color before = original.get_pixel(x, y);
            const Color after = fb.get_pixel(x, y);
            CHECK(after.r == doctest::Approx(before.r).epsilon(1.0e-6));
            CHECK(after.g == doctest::Approx(before.g).epsilon(1.0e-6));
            CHECK(after.b == doctest::Approx(before.b).epsilon(1.0e-6));
        }
    }

    // The lateral extremities have a high CoC and the 1-pixel checker must
    // average to a nearly flat grey. Measure the residual horizontal gradient
    // over a window that sits entirely in the peripheral zone.
    const float left_gradient = average_horizontal_gradient(fb, 2, 34, 90);
    const float right_gradient =
        average_horizontal_gradient(fb, kWidth - 34, kWidth - 2, 90);
    CHECK(left_gradient < 0.05f);
    CHECK(right_gradient < 0.05f);
    CHECK(effects::camera_ring_coc(p, 4.0f / kWidth, 0.5f) > 0.80f);

    // Normalized uniform weights preserve a constant image exactly.
    Framebuffer constant = make_image(64, 36, Color{0.4f, 0.3f, 0.2f, 1.0f});
    const Framebuffer constant_before = constant;
    renderer::apply_camera_ring(constant, p);
    CHECK(test_fx::framebuffer_max_error(constant, constant_before) <= 1.0e-6f);
}

// ===========================================================================
// Test 3 — Typographic three-zone check + luminous energy conservation
// ===========================================================================

TEST_CASE("CameraRing: center glyph edge stays sharp, lateral glyph edge "
          "defocuses, and total luminance is conserved") {
    constexpr int kWidth = 640;
    constexpr int kHeight = 360;
    Framebuffer fb = make_image(kWidth, kHeight, Color{0.0f, 0.0f, 0.0f, 1.0f});
    const Color ink{1.0f, 1.0f, 1.0f, 1.0f};
    fill_rect(fb, 30, 120, 90, 240, ink);      // lateral "cal" stroke
    fill_rect(fb, 308, 120, 332, 240, ink);    // central "or" stroke

    const Framebuffer original = fb;
    CameraRingParams p = fixture_params();
    renderer::apply_camera_ring(fb, p);

    const int center_edge = edge_width(fb, 180, 260, 380, 1.0f);
    const int lateral_edge = edge_width(fb, 180, 10, 110, 1.0f);
    REQUIRE(center_edge >= 0);
    REQUIRE(lateral_edge >= 0);
    CHECK(center_edge <= 2);
    CHECK(lateral_edge >= 6);
    CHECK(lateral_edge > center_edge * 3);

    const double before = total_luma(original);
    const double after = total_luma(fb);
    REQUIRE(before > 0.0);
    const double relative_error = std::abs(after - before) / before;
    CHECK(relative_error < 0.015);
}

// ===========================================================================
// Test 4 — Spectral isolation of radial chromatic dispersion
// ===========================================================================

TEST_CASE("CameraRing: chromatic dispersion is zero on-axis and radial at the "
          "lateral extremities") {
    constexpr int kWidth = 640;
    constexpr int kHeight = 360;
    Framebuffer fb = make_image(kWidth, kHeight, Color{0.0f, 0.0f, 0.0f, 1.0f});
    const Color ink{1.0f, 1.0f, 1.0f, 1.0f};
    // Keep the strokes inside the vertical safe-zone half-height so every
    // central-stroke pixel is genuinely on-axis at this fixture scale.
    fill_rect(fb, 30, 160, 90, 200, ink);
    fill_rect(fb, 308, 160, 332, 200, ink);

    CameraRingParams p = fixture_params();
    p.max_blur_radius = 0.03f;
    p.chroma_dispersion = 0.02f;
    renderer::apply_camera_ring(fb, p);

    // The central stroke lies in the exact-copy safe zone; no spectral error.
    CHECK(max_chroma_error(fb, 300, 155, 340, 205) <= 1.0e-6f);
    // The lateral stroke is outside the safe zone; R/B separation is visible.
    CHECK(max_chroma_error(fb, 10, 155, 110, 205) > 0.05f);
}

// ===========================================================================
// Test 5 — Dynamic rig coupling + deterministic performance proxy
// ===========================================================================

TEST_CASE("CameraRing: production kernel accepts 64 jittered taps") {
    CameraRingParams p = fixture_params();
    p.samples = 64;
    CHECK_NOTHROW(p.validate());
    p.samples = 65;
    CHECK_THROWS(p.validate());
}

TEST_CASE("CameraRing: camera speed narrows the safe zone and widens defocus") {
    CameraRingParams p = fixture_params();
    const auto rest = effects::camera_ring_coupled_optics(p, 0.0f);
    const auto half = effects::camera_ring_coupled_optics(p, 600.0f);
    const auto full = effects::camera_ring_coupled_optics(p, 1200.0f);
    const auto clamped = effects::camera_ring_coupled_optics(p, 5000.0f);

    CHECK(rest.inner_radius == doctest::Approx(p.inner_radius));
    CHECK(rest.max_blur_radius == doctest::Approx(p.max_blur_radius));
    CHECK(full.inner_radius == doctest::Approx(p.inner_radius * 0.5f));
    CHECK(full.max_blur_radius ==
          doctest::Approx(p.max_blur_radius * (28.0f / 12.0f)));
    CHECK(half.inner_radius < rest.inner_radius);
    CHECK(half.inner_radius > full.inner_radius);
    CHECK(half.max_blur_radius > rest.max_blur_radius);
    CHECK(half.max_blur_radius < full.max_blur_radius);
    CHECK(clamped.inner_radius == doctest::Approx(full.inner_radius));
    CHECK(clamped.max_blur_radius == doctest::Approx(full.max_blur_radius));
}

TEST_CASE("CameraRing: CPU proxy is deterministic and bounded") {
    constexpr int kWidth = 640;
    constexpr int kHeight = 360;
    Framebuffer a = make_image(kWidth, kHeight, Color{0.0f, 0.0f, 0.0f, 1.0f});
    fill_rect(a, 20, 140, 70, 220, Color{1.0f, 1.0f, 1.0f, 1.0f});
    fill_rect(a, 280, 140, 360, 220, Color{1.0f, 1.0f, 1.0f, 1.0f});
    fill_rect(a, 590, 140, 620, 220, Color{1.0f, 1.0f, 1.0f, 1.0f});
    Framebuffer b = a;
    CameraRingParams p = fixture_params();

    const auto start = std::chrono::steady_clock::now();
    renderer::apply_camera_ring(a, p);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    renderer::apply_camera_ring(b, p);

    MESSAGE("CameraRing 640x360, 16 taps: " << elapsed << " ms (CPU proxy)");
    CHECK(elapsed >= 0);
    CHECK(elapsed < 5000);
    CHECK(test_fx::framebuffer_max_error(a, b) <= 1.0e-6f);
}
