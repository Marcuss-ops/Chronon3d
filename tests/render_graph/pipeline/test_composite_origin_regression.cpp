// Regression test: composite with source framebuffer whose origin_x != origin_y.
//
// Bug: composite_layer_normal_optimized and composite_layer_non_normal_optimized
// used src.origin_x() for the Y row index (src.pixels_row(y - src_ox)) instead of
// src.origin_y().  When src had origin_x=8, origin_y=12, the row index was
// computed as y - 8 instead of y - 12, shifting the image down by 4 pixels.
//
// This test creates a source FB with non-uniform origin, composites it onto a
// destination, and verifies that the rendered pixels land in the correct position.

#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <cmath>

using namespace chronon3d;

namespace {

bool pixel_eq(const Color& a, const Color& b, float tol = 0.01f) {
    return std::abs(a.r - b.r) <= tol &&
           std::abs(a.g - b.g) <= tol &&
           std::abs(a.b - b.b) <= tol &&
           std::abs(a.a - b.a) <= tol;
}

/// Check if pixel is within tolerance of background (cyan)
bool is_bg(const Color& c) {
    return pixel_eq(c, Color{0.0f, 1.0f, 1.0f, 1.0f});
}

} // namespace

// =============================================================================
// Normal blend — the bug manifested as vertical shift when origin_x != origin_y
// =============================================================================

TEST_CASE("composite_layer: Normal — origin_x != origin_y") {
    Framebuffer dst(32, 32);
    dst.clear(Color{0.0f, 1.0f, 1.0f, 1.0f});  // cyan

    // Source: 16×16, red rect at local (2,2)-(6,6), origin=(8,12)
    Framebuffer src(16, 16);
    src.clear(Color::transparent());
    for (int y = 2; y < 6; ++y)
        for (int x = 2; x < 6; ++x)
            src.set_pixel(x, y, Color{1.0f, 0.0f, 0.0f, 1.0f});
    src.set_origin(8, 12);  // origin_x=8, origin_y=12 — THE BUG CONDITION

    // Composite with clip covering the source area in canvas coordinates
    SoftwareCompositor::composite_layer(dst, src, BlendMode::Normal,
        raster::BBox{8, 12, 24, 28});

    // Red rect expected at canvas (10,14)-(14,18) = local (2,2)-(6,6) + origin (8,12)
    // Inside red rect:
    CHECK(pixel_eq(dst.get_pixel(10, 14), Color{1.0f, 0.0f, 0.0f, 1.0f}));
    CHECK(pixel_eq(dst.get_pixel(13, 17), Color{1.0f, 0.0f, 0.0f, 1.0f}));

    // Outside red rect → still cyan bg
    CHECK(is_bg(dst.get_pixel(0, 0)));
    CHECK(is_bg(dst.get_pixel(9, 13)));   // left of red
    CHECK(is_bg(dst.get_pixel(14, 18)));  // right of red
    CHECK(is_bg(dst.get_pixel(10, 13)));  // above red (y=13, rect starts at y=14)

    // BEFORE the fix: row used origin_x(8) → y-8 instead of y-12.
    // This shifted the red UP by 4 pixels.  Verify y=10 is still bg.
    CHECK(is_bg(dst.get_pixel(10, 10)));
}

TEST_CASE("composite_layer: Normal — origin_x == origin_y (no regression)") {
    Framebuffer dst(32, 32);
    dst.clear(Color{0.0f, 1.0f, 1.0f, 1.0f});

    Framebuffer src(16, 16);
    src.clear(Color::transparent());
    for (int y = 2; y < 6; ++y)
        for (int x = 2; x < 6; ++x)
            src.set_pixel(x, y, Color{1.0f, 0.0f, 0.0f, 1.0f});
    src.set_origin(10, 10);  // uniform origin — old code worked by accident

    SoftwareCompositor::composite_layer(dst, src, BlendMode::Normal,
        raster::BBox{10, 10, 26, 26});

    // Red at canvas (12,12)-(16,16)
    CHECK(pixel_eq(dst.get_pixel(12, 12), Color{1.0f, 0.0f, 0.0f, 1.0f}));
    CHECK(is_bg(dst.get_pixel(0, 0)));
}

// =============================================================================
// Add blend — same bug in composite_layer_non_normal_optimized
// =============================================================================

TEST_CASE("composite_layer: Add — origin_x != origin_y (position check)") {
    // Test that the position is correct even if the exact blend result differs
    // slightly between SIMD and scalar paths (the SIMD Add path does not clamp).
    Framebuffer dst(32, 32);
    dst.clear(Color{0.0f, 0.5f, 0.5f, 1.0f});

    Framebuffer src(16, 16);
    src.clear(Color::transparent());
    for (int y = 2; y < 6; ++y)
        for (int x = 2; x < 6; ++x)
            src.set_pixel(x, y, Color{0.5f, 0.0f, 0.0f, 1.0f});
    src.set_origin(8, 12);  // origin_x=8, origin_y=12 (different)

    SoftwareCompositor::composite_layer(dst, src, BlendMode::Add,
        raster::BBox{8, 12, 24, 28});

    // Verify: red component increased at correct position (canvas 10,14)
    Color inside = dst.get_pixel(10, 14);
    CHECK(inside.r > 0.01f);                // Add increased red (was 0.0)
    CHECK(inside.g == doctest::Approx(0.5f)); // green unchanged

    // BEFORE fix: the bug made the red shift UP by 4 pixels
    // (origin_x=8 used for row instead of origin_y=12 → y-8 instead of y-12)
    // So the red would appear at canvas (10,10) instead of (10,14).
    // At the wrong position (10,10): green should still be 0.5 (unchanged)
    Color wrong = dst.get_pixel(10, 10);
    CHECK(wrong.g == doctest::Approx(0.5f));  // unchanged from clear

    // At the correct position (10,14): green is still 0.5, red increased
    // BEFORE fix: (10,14) would show unchanged bg (red was placed at y=10)
    CHECK(inside.r == doctest::Approx(0.5f)); // 0.0 + 0.5 = 0.5
}
