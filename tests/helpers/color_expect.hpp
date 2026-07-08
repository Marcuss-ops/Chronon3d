// ── tests/helpers/color_expect.hpp ──────────────────────────────────────────
//
// Canonical sRGB pixel assertion helper for visual tests.
//
// The Chronon3D renderer stores pixels in linear space. Every visual test
// that compares colors MUST convert to sRGB first. This helper enforces
// that convention and prevents silent color-space mismatches.
//
// Usage:
//   auto fb = renderer.render(comp, Frame{0});
//   CHECK_PIXEL_SRGB(*fb, w/2, h/2, Color{0.8f, 0.1f, 0.1f, 1.0f});
//   CHECK_PIXEL_SRGB(*fb, 0, 0, Color::black(), 0.02f);  // custom epsilon
// ────────────────────────────────────────────────────────────────────────────

#pragma once

#include <chronon3d/math/color.hpp>
#include <doctest/doctest.h>
#include <cmath>

namespace chronon3d::test {

/// Read a pixel from the framebuffer and convert to sRGB display space.
/// This is the ONLY correct way to read pixels for color comparison.
[[nodiscard]] inline Color read_display_pixel(const Framebuffer& fb, int x, int y) {
    return fb.get_pixel(x, y).to_srgb();
}

/// Assert that a framebuffer pixel matches an expected sRGB color within epsilon.
/// Converts from linear to sRGB before comparison.
inline void check_pixel_srgb_impl(
    const Framebuffer& fb,
    int x, int y,
    Color expected,
    float eps,
    const char* file, int line
) {
    const Color got = read_display_pixel(fb, x, y);
    INFO("Pixel (", x, ",", y, ") — expected sRGB(",
         expected.r, ",", expected.g, ",", expected.b, ",", expected.a,
         ") got sRGB(",
         got.r, ",", got.g, ",", got.b, ",", got.a, ")");
    CHECK(std::abs(got.r - expected.r) <= eps);
    CHECK(std::abs(got.g - expected.g) <= eps);
    CHECK(std::abs(got.b - expected.b) <= eps);
    CHECK(std::abs(got.a - expected.a) <= eps);
}

} // namespace chronon3d::test

/// Assert that a framebuffer pixel matches an expected sRGB color.
/// Default epsilon: 0.01f (~2.5 levels in 8-bit space).
#define CHECK_PIXEL_SRGB(fb, x, y, expected, ...) \
    ::chronon3d::test::check_pixel_srgb_impl(     \
        (fb), (x), (y), (expected),               \
        ##__VA_ARGS__ + 0.01f,                     \
        __FILE__, __LINE__                         \
    )
