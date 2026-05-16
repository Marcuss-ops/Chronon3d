#include <doctest/doctest.h>
#include <chronon3d/animation/interpolate.hpp>

using namespace chronon3d;

TEST_CASE("interpolate basics") {
    SUBCASE("Linear interpolation at midpoint") {
        CHECK(interpolate(50.0f, 0.0f, 100.0f, 0.0f, 1.0f) == doctest::Approx(0.5f));
    }

    SUBCASE("Lower bound clamping") {
        CHECK(interpolate(-10.0f, 0.0f, 100.0f, 0.0f, 1.0f) == 0.0f);
    }

    SUBCASE("Upper bound clamping") {
        CHECK(interpolate(120.0f, 0.0f, 100.0f, 0.0f, 1.0f) == 1.0f);
    }

    SUBCASE("Equal start and end points") {
        CHECK(interpolate(50.0f, 10.0f, 10.0f, 5.0f, 10.0f) == 5.0f);
    }

    SUBCASE("Frame overload") {
        CHECK(interpolate(static_cast<Frame>(50), static_cast<Frame>(0), static_cast<Frame>(100), 0.0f, 1.0f) == doctest::Approx(0.5f));
    }
}
