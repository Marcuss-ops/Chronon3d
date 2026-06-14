#pragma once

// ── Test Helpers ────────────────────────────────────────────────────────────
//
// Canonical tolerances, test images, and colour helpers shared across all
// effect tests (A1–A7).
//
// Usage:
//   #include "tests/effects/test_helpers.hpp"

#include <doctest/doctest.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace chronon3d;

// =============================================================================
// 1. Tolerances (section 2 della specifica)
// =============================================================================

constexpr float kExactEpsilon  = 1.0e-7f;
constexpr float kScalarEpsilon = 1.0e-6f;
constexpr float kSimdEpsilon   = 1.0e-5f;
constexpr float kBlurEpsilon   = 1.0e-4f;

// =============================================================================
// 2. check_color_near con isfinite (sezione 2)
// =============================================================================

inline void check_color_near(const Color& actual, const Color& expected,
                              float epsilon = kScalarEpsilon) {
    CHECK_MESSAGE(std::isfinite(actual.r), "r is finite");
    CHECK_MESSAGE(std::isfinite(actual.g), "g is finite");
    CHECK_MESSAGE(std::isfinite(actual.b), "b is finite");
    CHECK_MESSAGE(std::isfinite(actual.a), "a is finite");

    CHECK(std::abs(actual.r - expected.r) <= epsilon);
    CHECK(std::abs(actual.g - expected.g) <= epsilon);
    CHECK(std::abs(actual.b - expected.b) <= epsilon);
    CHECK(std::abs(actual.a - expected.a) <= epsilon);
}

// Variant that also checks bit-exact alpha when requested.
inline void check_color_near_alpha_exact(const Color& actual, const Color& expected,
                                          float epsilon = kScalarEpsilon) {
    CHECK_MESSAGE(std::isfinite(actual.r), "r is finite");
    CHECK_MESSAGE(std::isfinite(actual.g), "g is finite");
    CHECK_MESSAGE(std::isfinite(actual.b), "b is finite");
    CHECK_MESSAGE(std::isfinite(actual.a), "a is finite");

    CHECK(std::abs(actual.r - expected.r) <= epsilon);
    CHECK(std::abs(actual.g - expected.g) <= epsilon);
    CHECK(std::abs(actual.b - expected.b) <= epsilon);
    CHECK(actual.a == expected.a);  // alpha bit-exact
}

// =============================================================================
// 3. Immagini canoniche (sezione 1 della specifica)
// =============================================================================

// ── 3a. Colori premoltiplicati canonici ─────────────────────────────────
inline constexpr Color kOpaquePremul{0.8f, 0.4f, 0.2f, 1.0f};
inline constexpr Color kHalfAlphaPremul{0.4f, 0.2f, 0.1f, 0.5f};
inline constexpr Color kTransparent{0.0f, 0.0f, 0.0f, 0.0f};
inline constexpr Color kHdrPremul{2.0f, 0.5f, 4.0f, 1.0f};
inline constexpr Color kHdrHalfPremul{1.0f, 0.25f, 2.0f, 0.5f};
inline constexpr Color kNegativePremul{-0.2f, 0.3f, 1.5f, 1.0f};

// Straight RGB equivalent of kHalfAlphaPremul: (0.8, 0.4, 0.2, 0.5)
inline constexpr float kHalfStraightR = 0.8f;
inline constexpr float kHalfStraightG = 0.4f;
inline constexpr float kHalfStraightB = 0.2f;

// ── 3b. Impulso 5×5 ───────────────────────────────────────────────────
// Singolo pixel bianco opaco al centro (12,12).
// Usato per blur, radial blur, stroke e bounds.
[[nodiscard]] inline Framebuffer make_impulse_5x5() {
    Framebuffer fb(5, 5);
    fb.clear(Color::transparent());
    fb.set_pixel(2, 2, Color{1.0f, 1.0f, 1.0f, 1.0f});
    return fb;
}

// ── 3c. Rampa orizzontale 4×1 ─────────────────────────────────────────
// Valori: 0.0, 0.25, 0.50, 1.0
// Usato per exposure, levels, curves, offset e sampling.
[[nodiscard]] inline Framebuffer make_ramp_4x1() {
    Framebuffer fb(4, 1);
    fb.set_pixel(0, 0, Color{0.00f, 0.00f, 0.00f, 1.0f});
    fb.set_pixel(1, 0, Color{0.25f, 0.25f, 0.25f, 1.0f});
    fb.set_pixel(2, 0, Color{0.50f, 0.50f, 0.50f, 1.0f});
    fb.set_pixel(3, 0, Color{1.00f, 1.00f, 1.00f, 1.0f});
    return fb;
}

// ── 3d. Texture 2×2 solo rosso ─────────────────────────────────────────
// Valori:  0 1    (canale rosso)
//          2 3
// Usato per sampling bilinear esatto.
[[nodiscard]] inline Framebuffer make_red_2x2() {
    Framebuffer fb(2, 2);
    fb.set_pixel(0, 0, Color{0.0f, 0.0f, 0.0f, 1.0f});  // rosso = 0
    fb.set_pixel(1, 0, Color{1.0f, 0.0f, 0.0f, 1.0f});  // rosso = 1
    fb.set_pixel(0, 1, Color{2.0f, 0.0f, 0.0f, 1.0f});  // rosso = 2
    fb.set_pixel(1, 1, Color{3.0f, 0.0f, 0.0f, 1.0f});  // rosso = 3
    return fb;
}

// ── 3e. Maschera binaria 5×5 ──────────────────────────────────────────
// Quadrato opaco 3×3 al centro.
// Usato per Stroke, dilation ed erosion.
[[nodiscard]] inline Framebuffer make_binary_mask_5x5() {
    Framebuffer fb(5, 5);
    fb.clear(Color::transparent());
    for (int y = 1; y <= 3; ++y)
        for (int x = 1; x <= 3; ++x)
            fb.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, 1.0f});
    return fb;
}

// ── 3f. Framebuffer con coordinate nei pixel ──────────────────────────
// Ogni pixel contiene coordinate riconoscibili per test clip.
//   pixel.r = x;  pixel.g = y;  pixel.b = x + y;  pixel.a = 1.0f
[[nodiscard]] inline Framebuffer make_coord_fb(int w, int h) {
    Framebuffer fb(w, h);
    for (int y = 0; y < h; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            row[x] = Color{static_cast<float>(x),
                           static_cast<float>(y),
                           static_cast<float>(x + y),
                           1.0f};
        }
    }
    return fb;
}

// =============================================================================
// 4. Helper: somma alpha (per test di blur/stroke/energy)
// =============================================================================

[[nodiscard]] inline float sum_alpha(const Framebuffer& fb) {
    float total = 0.0f;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            total += row[x].a;
        }
    }
    return total;
}

// =============================================================================
// 5. Helper: errore massimo tra due framebuffer
// =============================================================================

[[nodiscard]] inline float framebuffer_max_error(const Framebuffer& a,
                                                   const Framebuffer& b) {
    CHECK(a.width() == b.width());
    CHECK(a.height() == b.height());

    float max_err = 0.0f;
    for (int y = 0; y < a.height(); ++y) {
        const Color* ra = a.pixels_row(y);
        const Color* rb = b.pixels_row(y);
        for (int x = 0; x < a.width(); ++x) {
            max_err = std::max(max_err, std::abs(ra[x].r - rb[x].r));
            max_err = std::max(max_err, std::abs(ra[x].g - rb[x].g));
            max_err = std::max(max_err, std::abs(ra[x].b - rb[x].b));
            max_err = std::max(max_err, std::abs(ra[x].a - rb[x].a));
        }
    }
    return max_err;
}
