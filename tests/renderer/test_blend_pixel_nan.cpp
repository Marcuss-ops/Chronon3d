#include <doctest/doctest.h>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>
#include <limits>

// blend_pixel lives in the backend detail header — include it directly
// since this test specifically validates the NaN/Inf guard we added there.
// CMAKE_SOURCE_DIR is the project root, so this path resolves correctly.
#include "src/backends/software/utils/blend2d_bridge_detail.hpp"
using namespace chronon3d;


namespace {

bool is_transparent(const Color& c) {
    return c.r == 0.0f && c.g == 0.0f && c.b == 0.0f && c.a == 0.0f;
}

bool has_nan_or_inf(const Color& c) {
    return std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a) ||
           std::isinf(c.r) || std::isinf(c.g) || std::isinf(c.b) || std::isinf(c.a);
}

} // namespace

// =============================================================================
// blend_pixel() NaN / Inf guards
// =============================================================================

TEST_CASE("blend_pixel: NaN src sets dst to transparent (Normal mode)") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color nan_src{std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 1.0f};

    blend2d_bridge::detail::blend_pixel(dst, nan_src, BlendMode::Normal);

    CHECK(is_transparent(dst));
}

TEST_CASE("blend_pixel: NaN dst sets dst to transparent") {
    Color dst{std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 1.0f};
    Color src{0.5f, 0.5f, 0.5f, 1.0f};

    blend2d_bridge::detail::blend_pixel(dst, src, BlendMode::Normal);

    CHECK(is_transparent(dst));
}

TEST_CASE("blend_pixel: Inf src sets dst to transparent") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color inf_src{std::numeric_limits<float>::infinity(), 0.5f, 0.5f, 1.0f};

    blend2d_bridge::detail::blend_pixel(dst, inf_src, BlendMode::Normal);

    CHECK(is_transparent(dst));
}

TEST_CASE("blend_pixel: NaN in Add mode sets dst to transparent") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color nan_src{0.5f, std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f};

    blend2d_bridge::detail::blend_pixel(dst, nan_src, BlendMode::Add);

    CHECK(is_transparent(dst));
}

TEST_CASE("blend_pixel: NaN alpha sets dst to transparent") {
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};
    Color nan_alpha{0.5f, 0.5f, 0.5f, std::numeric_limits<float>::quiet_NaN()};

    blend2d_bridge::detail::blend_pixel(dst, nan_alpha, BlendMode::Normal);

    CHECK(is_transparent(dst));
}

TEST_CASE("blend_pixel: -Inf dst sets dst to transparent") {
    Color dst{0.5f, -std::numeric_limits<float>::infinity(), 0.5f, 1.0f};
    Color src{0.8f, 0.2f, 0.4f, 0.5f};

    blend2d_bridge::detail::blend_pixel(dst, src, BlendMode::Normal);

    CHECK(is_transparent(dst));
}

// =============================================================================
// blend_pixel() Add mode clamping
// =============================================================================

TEST_CASE("blend_pixel: Add mode clamps to 1.0") {
    Color dst{0.6f, 0.6f, 0.6f, 0.6f};
    Color src{0.6f, 0.6f, 0.6f, 0.6f};
    // Without clamping: 1.2, 1.2, 1.2, 1.2

    blend2d_bridge::detail::blend_pixel(dst, src, BlendMode::Add);

    CHECK(dst.r <= 1.0f);
    CHECK(dst.g <= 1.0f);
    CHECK(dst.b <= 1.0f);
    CHECK(dst.a <= 1.0f);
}

TEST_CASE("blend_pixel: Add mode with small values blends correctly") {
    Color dst{0.1f, 0.2f, 0.3f, 0.4f};
    Color src{0.3f, 0.2f, 0.1f, 0.2f};
    // Expected sum: 0.4, 0.4, 0.4, 0.6

    blend2d_bridge::detail::blend_pixel(dst, src, BlendMode::Add);

    CHECK(dst.r == doctest::Approx(0.4f));
    CHECK(dst.g == doctest::Approx(0.4f));
    CHECK(dst.b == doctest::Approx(0.4f));
    CHECK(dst.a == doctest::Approx(0.6f));
}

// =============================================================================
// blend_pixel() normal path correctness (no false positive)
// =============================================================================

TEST_CASE("blend_pixel: clean values produce no NaN/Inf") {
    Color dst{0.2f, 0.4f, 0.6f, 1.0f};
    Color src{0.8f, 0.2f, 0.0f, 0.5f};

    blend2d_bridge::detail::blend_pixel(dst, src, BlendMode::Normal);

    CHECK_FALSE(has_nan_or_inf(dst));
}

TEST_CASE("blend_pixel: fully opaque src replaces dst in Normal mode") {
    Color dst{0.5f, 0.3f, 0.1f, 1.0f};
    Color src{0.8f, 0.2f, 0.4f, 1.0f};

    blend2d_bridge::detail::blend_pixel(dst, src, BlendMode::Normal);

    // src.a = 1.0, so dst should become src
    CHECK(dst.r == doctest::Approx(0.8f));
    CHECK(dst.g == doctest::Approx(0.2f));
    CHECK(dst.b == doctest::Approx(0.4f));
    CHECK(dst.a == doctest::Approx(1.0f));
}

TEST_CASE("blend_pixel: safe values across blend modes never produce NaN") {
    Color colors[] = {
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.5f, 0.3f, 0.7f, 0.3f},
        {0.999f, 0.001f, 0.5f, 0.999f},
    };

    BlendMode modes[] = {BlendMode::Normal, BlendMode::Add};

    for (auto mode : modes) {
        for (const auto& src : colors) {
            for (auto dst_val : colors) {
                Color dst = dst_val; // copy since blend_pixel mutates
                blend2d_bridge::detail::blend_pixel(dst, src, mode);
                CHECK_FALSE(has_nan_or_inf(dst));
            }
        }
    }
}
