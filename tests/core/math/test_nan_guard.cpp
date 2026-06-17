#include <doctest/doctest.h>
#include <chronon3d/compositor/alpha.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <cmath>
#include <limits>
using namespace chronon3d;


namespace {

// Helper: check that a Color is fully transparent (all channels zero).
bool is_transparent(const Color& c) {
    return c.r == 0.0f && c.g == 0.0f && c.b == 0.0f && c.a == 0.0f;
}

bool has_nan_or_inf(const Color& c) {
    return std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a) ||
           std::isinf(c.r) || std::isinf(c.g) || std::isinf(c.b) || std::isinf(c.a);
}

} // namespace

// =============================================================================
// NaN guard: compositor::blend()
// =============================================================================

TEST_CASE("NaN guard: compositor::blend with NaN src returns transparent") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color nan_src{std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 1.0f};

    Color result = compositor::blend(nan_src, dst, BlendMode::Normal);
    CHECK(is_transparent(result));
}

TEST_CASE("NaN guard: compositor::blend with NaN dst returns transparent") {
    Color dst{std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 1.0f};
    Color src{0.5f, 0.5f, 0.5f, 1.0f};

    Color result = compositor::blend(src, dst, BlendMode::Normal);
    CHECK(is_transparent(result));
}

TEST_CASE("NaN guard: compositor::blend with Inf src returns transparent") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color inf_src{std::numeric_limits<float>::infinity(), 0.5f, 0.5f, 1.0f};

    Color result = compositor::blend(inf_src, dst, BlendMode::Normal);
    CHECK(is_transparent(result));
}

TEST_CASE("NaN guard: compositor::blend with -Inf dst returns transparent") {
    Color dst{-std::numeric_limits<float>::infinity(), 0.5f, 0.5f, 1.0f};
    Color src{0.5f, 0.5f, 0.5f, 1.0f};

    Color result = compositor::blend(src, dst, BlendMode::Normal);
    CHECK(is_transparent(result));
}

TEST_CASE("NaN guard: compositor::blend with NaN alpha returns transparent") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color nan_alpha{0.5f, 0.5f, 0.5f, std::numeric_limits<float>::quiet_NaN()};

    Color result = compositor::blend(nan_alpha, dst, BlendMode::Normal);
    CHECK(is_transparent(result));
}

TEST_CASE("NaN guard: compositor::blend with NaN in Add mode returns transparent") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color nan_src{std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 1.0f};

    Color result = compositor::blend(nan_src, dst, BlendMode::Add);
    CHECK(is_transparent(result));
}

TEST_CASE("NaN guard: compositor::blend clean values are unchanged") {
    Color dst{0.2f, 0.4f, 0.6f, 1.0f};
    Color src{0.8f, 0.2f, 0.0f, 0.5f};

    Color result = compositor::blend(src, dst, BlendMode::Normal);
    CHECK_FALSE(has_nan_or_inf(result));
    CHECK_FALSE(is_transparent(result)); // should blend normally
}

// =============================================================================
// NaN guard: blend_normal()
// =============================================================================

TEST_CASE("NaN guard: blend_normal with NaN src returns transparent") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color nan_src{std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 1.0f};

    Color result = compositor::blend_normal(nan_src, dst);
    CHECK(is_transparent(result));
}

TEST_CASE("NaN guard: blend_normal with NaN dst returns transparent") {
    Color dst{std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 1.0f};
    Color src{0.5f, 0.5f, 0.5f, 1.0f};

    Color result = compositor::blend_normal(src, dst);
    CHECK(is_transparent(result));
}

TEST_CASE("NaN guard: blend_normal with Inf returns transparent") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color inf_src{std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.5f};

    Color result = compositor::blend_normal(inf_src, dst);
    CHECK(is_transparent(result));
}

TEST_CASE("NaN guard: blend_normal clean values produce no NaN") {
    Color dst{0.2f, 0.4f, 0.6f, 1.0f};
    Color src{0.8f, 0.2f, 0.0f, 0.5f};

    Color result = compositor::blend_normal(src, dst);
    CHECK_FALSE(has_nan_or_inf(result));
}

// =============================================================================
// Add mode clamping
// =============================================================================

TEST_CASE("Add mode: compositor::blend Add mode supports HDR (>1.0)") {
    // compositor::blend() is a pure math function that does NOT clamp.
    // Clamping is handled at the framebuffer-write boundary.
    // HDR values >1.0 are valid for glow, bloom, etc.
    Color dst{0.6f, 0.6f, 0.6f, 0.6f};
    Color src{0.6f, 0.6f, 0.6f, 0.6f};
    // Expected: 1.2, 1.2, 1.2, 1.2 (no clamp)

    Color result = compositor::blend(src, dst, BlendMode::Add);
    CHECK(result.r == doctest::Approx(1.2f));
    CHECK(result.g == doctest::Approx(1.2f));
    CHECK(result.b == doctest::Approx(1.2f));
    CHECK(result.a == doctest::Approx(1.2f));
}

TEST_CASE("Add mode clamp: compositor::blend Add mode with small values doesn't clamp unnecessarily") {
    Color dst{0.1f, 0.2f, 0.3f, 0.4f};
    Color src{0.3f, 0.2f, 0.1f, 0.2f};

    Color result = compositor::blend(src, dst, BlendMode::Add);
    CHECK(result.r == doctest::Approx(0.4f));
    CHECK(result.g == doctest::Approx(0.4f));
    CHECK(result.b == doctest::Approx(0.4f));
    CHECK(result.a == doctest::Approx(0.6f));
}

TEST_CASE("Add mode clamp: compositor::blend Add does not clamp lower bound (negative values pass through)") {
    // Add mode only clamps the upper bound to 1.0, not the lower bound to 0.
    // Negative values are valid for subtractive/additive blending effects.
    // Use non-zero alpha so the Add branch is actually taken.
    Color dst{-0.1f, 0.2f, 0.0f, 0.5f};
    Color src{0.0f, -0.3f, 0.0f, 0.3f};

    Color result = compositor::blend(src, dst, BlendMode::Add);
    // Values should remain negative (not clamped to 0)
    CHECK(result.r < 0.0f);
    CHECK(result.g < 0.0f);
}

// =============================================================================
// Framebuffer::shift() vacated area clearing
// =============================================================================

TEST_CASE("Framebuffer::shift right clears vacated left columns") {
    Framebuffer fb(16, 8);
    fb.clear(Color{1.0f, 0.0f, 0.0f, 1.0f}); // fill with red

    fb.shift(4, 0); // shift right by 4

    // Vacated left columns (x=0..3) should be transparent
    for (i32 y = 0; y < fb.height(); ++y) {
        Color c = fb.get_pixel(0, y);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.g == doctest::Approx(0.0f));
        CHECK(c.b == doctest::Approx(0.0f));
        CHECK(c.a == doctest::Approx(0.0f));
    }
    // Shifted content should still be red where copied
    Color c = fb.get_pixel(4, 0);
    CHECK(c.r == doctest::Approx(1.0f));
}

TEST_CASE("Framebuffer::shift left clears vacated right columns") {
    Framebuffer fb(16, 8);
    fb.clear(Color{0.0f, 1.0f, 0.0f, 1.0f}); // fill with green

    fb.shift(-4, 0); // shift left by 4

    // Vacated right columns (x=12..15) should be transparent
    for (i32 y = 0; y < fb.height(); ++y) {
        Color c = fb.get_pixel(15, y);
        CHECK(c.r == doctest::Approx(0.0f));
        CHECK(c.a == doctest::Approx(0.0f));
    }
    // Shifted content should still be green at its new position
    Color c = fb.get_pixel(0, 0);
    CHECK(c.g == doctest::Approx(1.0f));
}

TEST_CASE("Framebuffer::shift down clears vacated top rows") {
    Framebuffer fb(8, 16);
    fb.clear(Color{0.0f, 0.0f, 1.0f, 1.0f}); // fill with blue

    fb.shift(0, 3); // shift down by 3

    // Vacated top rows (y=0..2) should be transparent
    for (i32 x = 0; x < fb.width(); ++x) {
        Color c = fb.get_pixel(x, 0);
        CHECK(c.a == doctest::Approx(0.0f));
    }
}

TEST_CASE("Framebuffer::shift up clears vacated bottom rows") {
    Framebuffer fb(8, 16);
    fb.clear(Color{1.0f, 1.0f, 1.0f, 1.0f}); // fill with white

    fb.shift(0, -5); // shift up by 5

    // Vacated bottom rows (y=11..15) should be transparent
    for (i32 x = 0; x < fb.width(); ++x) {
        Color c = fb.get_pixel(x, 15);
        CHECK(c.a == doctest::Approx(0.0f));
    }
}

TEST_CASE("Framebuffer::shift diagonal clears both vacated strips") {
    Framebuffer fb(16, 16);
    fb.clear(Color{1.0f, 0.5f, 0.0f, 1.0f}); // orange

    fb.shift(3, 2); // shift right by 3 and down by 2

    // Top-left corner of vacated area should be transparent
    Color c = fb.get_pixel(0, 0);
    CHECK(c.a == doctest::Approx(0.0f));
    // Top-right area (x >= 3, y < 2) is vacated vertically
    c = fb.get_pixel(5, 0);
    CHECK(c.a == doctest::Approx(0.0f));
    // Bottom-left area (x < 3, y >= 2) is vacated horizontally
    c = fb.get_pixel(0, 5);
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("Framebuffer::shift full shift out of bounds clears everything") {
    Framebuffer fb(8, 8);
    fb.clear(Color{1.0f, 0.0f, 0.0f, 1.0f}); // red

    fb.shift(100, 100); // shift way beyond bounds

    // Everything should be transparent
    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            CHECK(c.a == doctest::Approx(0.0f));
        }
    }
}

TEST_CASE("Framebuffer::shift with zero displacement does nothing") {
    Framebuffer fb(8, 8);
    fb.clear(Color{0.0f, 1.0f, 0.0f, 1.0f}); // green

    fb.shift(0, 0);

    // All pixels should still be green
    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            CHECK(c.g == doctest::Approx(1.0f));
        }
    }
}

// =============================================================================
// Framebuffer NaN contamination prevention
// =============================================================================

TEST_CASE("NaN guard: safe values never produce NaN through blend_normal") {
    // Test with a variety of normal (non-NaN) inputs to ensure the guard
    // doesn't false-positive.
    Color ref{0.0f, 0.0f, 0.0f, 0.0f};
    Color colors[] = {
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.5f, 0.3f, 0.7f, 0.3f},
        {0.999f, 0.001f, 0.5f, 0.999f},
    };

    for (const auto& src : colors) {
        for (const auto& dst : colors) {
            Color result = compositor::blend_normal(src, dst);
            CHECK_FALSE(has_nan_or_inf(result));
        }
    }
}
