#include <doctest/doctest.h>
#include <chronon3d/animation/spring.hpp>

using namespace chronon3d;

TEST_CASE("Spring animation logic") {
    f32 from = 0.0f;
    f32 to = 100.0f;

    SUBCASE("Frame 0 starts at from") {
        f32 val = spring(0.0f, from, to);
        CHECK(val == doctest::Approx(from));
    }

    SUBCASE("High time converges to to") {
        f32 val = spring(10.0f, from, to);
        CHECK(val == doctest::Approx(to).epsilon(0.01));
    }

    SUBCASE("Deterministic results") {
        f32 val1 = spring(1.5f, from, to);
        f32 val2 = spring(1.5f, from, to);
        CHECK(val1 == val2);
    }

    SUBCASE("Damping affects convergence") {
        SpringConfig low_damping{.damping = 5.0f};
        SpringConfig high_damping{.damping = 50.0f};

        f32 val_high = spring(3.0f, from, to, high_damping);
        CHECK(val_high == doctest::Approx(to).epsilon(0.01));
    }
}
