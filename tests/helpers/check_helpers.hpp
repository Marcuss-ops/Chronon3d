#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// check_helpers.hpp — Shared test tolerances and assertion helpers
//
// Usage:
//   check_close(actual, expected, 1e-6f);
//   check_color_close(actual, expected, 1e-5f);
//   check_finite(color);
//   REQUIRE_COLOR_EQ(lhs, rhs);  // exact equality via doctest
// ──────────────────────────────────────────────────────────────────────────────

#include <doctest/doctest.h>
#include <chronon3d/math/color.hpp>
#include <cmath>
#include <limits>

namespace chronon3d::test {

// ── Tolerance table (from spec) ──────────────────────────────────────────────
//
// | Type of test                     | Tolerance  |
// | -------------------------------- | ---------- |
// | Indices, hash, enum, int coords  | exact      |
// | Scalar formula                   | 1e-6       |
// | Bilinear sampling                | 1e-6       |
// | Scalar vs Highway                | 1e-5       |
// | Effects with many samples        | 5e-5       |
// | Full float pipeline              | 1e-4       |
// | Golden PNG                       | ~1-2/255   |

inline constexpr float kEpsilonScalar   = 1e-6f;
inline constexpr float kEpsilonSIMD     = 1e-5f;
inline constexpr float kEpsilonPipeline = 1e-4f;

// ── Finite check ─────────────────────────────────────────────────────────────

/// Check that every component of a Color is finite (not NaN or Inf).
/// Automatically CHECK-fails if any component is non-finite.
inline void check_finite(const Color& c) {
    CHECK_MESSAGE(std::isfinite(c.r), "r=" << c.r);
    CHECK_MESSAGE(std::isfinite(c.g), "g=" << c.g);
    CHECK_MESSAGE(std::isfinite(c.b), "b=" << c.b);
    CHECK_MESSAGE(std::isfinite(c.a), "a=" << c.a);
}

/// Check that every component of an array of N colors is finite.
template <size_t N>
inline void check_finite_all(const Color (&colors)[N]) {
    for (size_t i = 0; i < N; ++i) {
        check_finite(colors[i]);
    }
}

// ── Close comparisons ────────────────────────────────────────────────────────

/// Assert that |actual - expected| <= epsilon.  Both must be finite.
inline void check_close(float actual, float expected, float epsilon = kEpsilonScalar) {
    CHECK_MESSAGE(std::isfinite(actual), "actual not finite: " << actual);
    CHECK_MESSAGE(std::isfinite(expected), "expected not finite: " << expected);
    CHECK_MESSAGE(std::abs(actual - expected) <= epsilon,
                  "|" << actual << " - " << expected << "| = " << std::abs(actual - expected)
                  << " > " << epsilon);
}

/// Assert that each component of `actual` is within `epsilon` of `expected`.
/// Also checks that both colors are finite.
inline void check_color_close(const Color& actual, const Color& expected,
                               float epsilon = kEpsilonScalar) {
    check_finite(actual);
    check_finite(expected);
    check_close(actual.r, expected.r, epsilon);
    check_close(actual.g, expected.g, epsilon);
    check_close(actual.b, expected.b, epsilon);
    check_close(actual.a, expected.a, epsilon);
}

// ── Premultiplied alpha round-trip ───────────────────────────────────────────

/// Check the premultiply/unpremultiply round-trip and representation.
/// Used by nearly all effect tests.
inline void test_premultiply_roundtrip() {
    const Color straight{0.8f, 0.4f, 0.2f, 0.5f};
    const Color expected_premul{0.4f, 0.2f, 0.1f, 0.5f};

    // Check premultiplied representation
    Color premul = straight.premultiplied();
    check_color_close(premul, expected_premul);

    // Check round-trip back to straight
    Color restored = premul.unpremultiplied();
    check_color_close(restored, straight);

    // Fully transparent with garbage RGB → must restore to transparent
    const Color invalid_hidden_rgb{100.0f, -50.0f, 20.0f, 0.0f};
    Color result = invalid_hidden_rgb.unpremultiplied();
    check_color_close(result, Color::transparent());
}

} // namespace chronon3d::test

// ── Doctest-style macro for check_close ─────────────────────────────────────
// These work inside TEST_CASE blocks without explicit namespace prefix.

#define CHECK_CLOSE(actual, expected) \
    chronon3d::test::check_close((actual), (expected))

#define CHECK_CLOSE_EPS(actual, expected, eps) \
    chronon3d::test::check_close((actual), (expected), (eps))

#define CHECK_COLOR_CLOSE(actual, expected) \
    chronon3d::test::check_color_close((actual), (expected))

#define CHECK_COLOR_CLOSE_EPS(actual, expected, eps) \
    chronon3d::test::check_color_close((actual), (expected), (eps))

#define CHECK_FINITE(c) \
    chronon3d::test::check_finite((c))
