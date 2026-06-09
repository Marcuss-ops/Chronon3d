#include <doctest/doctest.h>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>

using namespace chronon3d;

TEST_CASE("CubicBezier basic easing and solvers") {
    SUBCASE("Linear cubic-bezier(0,0,1,1) matches t exactly") {
        auto cb = CubicBezier(0.0f, 0.0f, 1.0f, 1.0f);
        for (int i = 0; i <= 10; ++i) {
            f32 t = static_cast<f32>(i) / 10.0f;
            REQUIRE(doctest::Approx(cb.sample(t)) == t);
        }
    }

    SUBCASE("CSS ease cubic-bezier(0.25, 0.1, 0.25, 1) boundary conditions") {
        auto cb = CubicBezier(0.25f, 0.10f, 0.25f, 1.00f);
        REQUIRE(cb.sample(0.0f) == 0.0f);
        REQUIRE(cb.sample(1.0f) == 1.0f);
        
        // Output should be monotonic (or at least strictly bounded in [0, 1] for this curve)
        f32 prev = 0.0f;
        for (int i = 1; i <= 20; ++i) {
            f32 t = static_cast<f32>(i) / 20.0f;
            f32 val = cb.sample(t);
            REQUIRE(val >= prev);
            REQUIRE(val <= 1.0f);
            prev = val;
        }
    }

    SUBCASE("CSS ease-in-out cubic-bezier(0.42, 0, 0.58, 1) matching values") {
        auto ease_curve = EasingCurve::cubic_bezier(0.42f, 0.0f, 0.58f, 1.0f);
        REQUIRE(ease_curve.apply(0.0f) == 0.0f);
        REQUIRE(ease_curve.apply(0.5f) == doctest::Approx(0.5f));
        REQUIRE(ease_curve.apply(1.0f) == 1.0f);
    }
}

TEST_CASE("AnimatedValue custom easing interpolation") {
    SUBCASE("Chaining AnimatedValue keys with custom cubic bezier curves") {
        auto ease = EasingCurve::cubic_bezier(0.25f, 0.1f, 0.25f, 1.0f);
        
        AnimatedValue<f32> x{0.0f};
        x.key(0, 0.0f, ease)
         .key(60, 100.0f);

        REQUIRE(x.evaluate(0) == 0.0f);
        REQUIRE(x.evaluate(60) == 100.0f);
        
        // Midpoint should be between 0 and 100, and follow the cubic Bezier curve
        f32 mid = x.evaluate(30);
        REQUIRE(mid > 0.0f);
        REQUIRE(mid < 100.0f);
        
        // Verify monotonicity
        f32 prev = 0.0f;
        for (Frame f = 0; f <= 60; f += 5) {
            f32 val = x.evaluate(f);
            REQUIRE(val >= prev);
            REQUIRE(val <= 100.0f);
            prev = val;
        }
    }
}
