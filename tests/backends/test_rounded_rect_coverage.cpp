#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/backends/assets/rounded_rect_coverage.hpp>

#include <array>

namespace {

using chronon3d::detail::rounded_rect_coverage;

constexpr float kWidth = 1600.0f;
constexpr float kHeight = 900.0f;
constexpr float kRadius = 22.0f;

} // namespace

TEST_CASE("rounded rectangle keeps all four straight edges opaque") {
    const float mid_x = kWidth * 0.5f;
    const float mid_y = kHeight * 0.5f;

    // With radius 22 the old image path returned a 1 - dist/r falloff for
    // every pixel in the left/right 22 px band. These samples are all on the
    // straight edges and therefore must be fully opaque.
    for (int i = 0; i < static_cast<int>(kRadius); ++i) {
        const float offset = static_cast<float>(i) + 0.5f;
        CHECK(rounded_rect_coverage(offset, mid_y, kWidth, kHeight, kRadius) ==
              doctest::Approx(1.0f));
        CHECK(rounded_rect_coverage(kWidth - offset, mid_y, kWidth, kHeight,
                                    kRadius) == doctest::Approx(1.0f));
        CHECK(rounded_rect_coverage(mid_x, offset, kWidth, kHeight, kRadius) ==
              doctest::Approx(1.0f));
        CHECK(rounded_rect_coverage(mid_x, kHeight - offset, kWidth, kHeight,
                                    kRadius) == doctest::Approx(1.0f));
    }

    CHECK(rounded_rect_coverage(0.5f, mid_y, kWidth, kHeight, kRadius) ==
          doctest::Approx(1.0f));
    CHECK(rounded_rect_coverage(mid_x, 0.5f, kWidth, kHeight, kRadius) ==
          doctest::Approx(1.0f));
    CHECK(rounded_rect_coverage(mid_x, mid_y, kWidth, kHeight, kRadius) ==
          doctest::Approx(1.0f));
}

TEST_CASE("rounded rectangle clips only the four corner arcs") {
    constexpr std::array<float, 4> xs{0.5f, kWidth - 0.5f, 0.5f, kWidth - 0.5f};
    constexpr std::array<float, 4> ys{0.5f, 0.5f, kHeight - 0.5f,
                                      kHeight - 0.5f};

    for (std::size_t i = 0; i < xs.size(); ++i) {
        CHECK(rounded_rect_coverage(xs[i], ys[i], kWidth, kHeight, kRadius) ==
              doctest::Approx(0.0f));
    }

    // A point just inside the corner radius is still part of the arc; the
    // coverage must be partial, not a hard transparent pixel.
    const float arc = rounded_rect_coverage(6.5f, 6.5f, kWidth, kHeight,
                                             kRadius);
    CHECK(arc > 0.0f);
    CHECK(arc < 1.0f);
}

TEST_CASE("zero radius disables corner clipping") {
    CHECK(rounded_rect_coverage(0.5f, 0.5f, kWidth, kHeight, 0.0f) ==
          doctest::Approx(1.0f));
    CHECK(rounded_rect_coverage(kWidth - 0.5f, kHeight - 0.5f, kWidth, kHeight,
                                0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("radius is clamped to half of the shorter side") {
    constexpr float side = 100.0f;
    CHECK(rounded_rect_coverage(0.5f, 0.5f, side, side, 1000.0f) ==
          doctest::Approx(0.0f));
    CHECK(rounded_rect_coverage(0.5f, side * 0.5f, side, side, 1000.0f) ==
          doctest::Approx(1.0f));
    CHECK(rounded_rect_coverage(side * 0.5f, side * 0.5f, side, side,
                                1000.0f) == doctest::Approx(1.0f));
}
