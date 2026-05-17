#include <doctest/doctest.h>
#include <chronon3d/compositor/blend_mode.hpp>

using namespace chronon3d;
using namespace chronon3d::compositor;

TEST_CASE("Test 11.1 — Normal blend mode math") {
    // Normal blend: src over dst
    Color src{1.0f, 0.0f, 0.0f, 0.5f}; // 50% opacity red
    Color dst{0.0f, 0.0f, 1.0f, 1.0f}; // opaque blue

    Color res = blend(src, dst, BlendMode::Normal);
    // Red over Blue at 50% opacity should blend colors
    CHECK(res.r == doctest::Approx(0.5f));
    CHECK(res.b == doctest::Approx(0.5f));
    CHECK(res.a == doctest::Approx(1.0f));
}

TEST_CASE("Test 11.2 — Add blend mode math") {
    Color src{0.5f, 0.0f, 0.0f, 0.5f};
    Color dst{0.3f, 0.0f, 0.0f, 0.5f};

    Color res = blend(src, dst, BlendMode::Add);
    CHECK(res.r == doctest::Approx(0.8f));
    CHECK(res.a == doctest::Approx(1.0f)); // 0.5 + 0.5 clamped to 1.0
}

TEST_CASE("Test 11.3 — Multiply blend mode math") {
    Color src{0.8f, 0.8f, 0.8f, 1.0f};
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};

    Color res = blend(src, dst, BlendMode::Multiply);
    CHECK(res.r == doctest::Approx(0.4f));
    CHECK(res.g == doctest::Approx(0.4f));
}

TEST_CASE("Test 11.4 — Screen blend mode math") {
    Color src{0.8f, 0.8f, 0.8f, 1.0f};
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};

    // 1 - (1 - 0.8) * (1 - 0.5) = 1 - 0.2 * 0.5 = 1 - 0.1 = 0.9
    Color res = blend(src, dst, BlendMode::Screen);
    CHECK(res.r == doctest::Approx(0.9f));
}

TEST_CASE("Test 11.5 — Overlay blend mode math") {
    Color src{0.3f, 0.8f, 0.0f, 1.0f};
    Color dst{0.4f, 0.6f, 0.0f, 1.0f};

    // overlay logic:
    // for r: dst < 0.5 -> 2.0 * s * d = 2.0 * 0.3 * 0.4 = 0.24
    // for g: dst >= 0.5 -> 1.0 - 2.0 * (1 - 0.8) * (1 - 0.6) = 1.0 - 2.0 * 0.2 * 0.4 = 1.0 - 0.16 = 0.84
    Color res = blend(src, dst, BlendMode::Overlay);
    CHECK(res.r == doctest::Approx(0.24f));
    CHECK(res.g == doctest::Approx(0.84f));
}

TEST_CASE("Test 11.6 — Blend mode guard: opacity 0 leaves background unchanged") {
    Color src{1.0f, 0.0f, 0.0f, 0.0f}; // fully transparent red
    Color dst{0.0f, 1.0f, 0.0f, 1.0f}; // green

    for (auto mode : {BlendMode::Normal, BlendMode::Add, BlendMode::Multiply, BlendMode::Screen, BlendMode::Overlay}) {
        Color res = blend(src, dst, mode);
        // Green color should remain unchanged
        CHECK(res.g == doctest::Approx(1.0f));
        CHECK(res.r == doctest::Approx(0.0f));
        CHECK(res.a == doctest::Approx(1.0f));
    }
}

TEST_CASE("Test 11.7 — Blend mode guard: opacity 1 covers background under Normal mode") {
    Color src{1.0f, 0.0f, 0.0f, 1.0f}; // opaque red
    Color dst{0.0f, 1.0f, 0.0f, 1.0f}; // green

    Color res = blend(src, dst, BlendMode::Normal);
    CHECK(res.r == doctest::Approx(1.0f));
    CHECK(res.g == doctest::Approx(0.0f));
    CHECK(res.a == doctest::Approx(1.0f));
}
