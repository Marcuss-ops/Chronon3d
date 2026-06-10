#include <doctest/doctest.h>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/math/glm_types.hpp>

using namespace chronon3d;

TEST_CASE("AnimatedValue expression uses default value as value variable") {
    AnimatedValue<f32> v{10.0f};
    v.expression("value + 5");

    CHECK(v.evaluate(0) == doctest::Approx(15.0f));
}

TEST_CASE("AnimatedValue expression receives frame variable") {
    AnimatedValue<f32> v{10.0f};
    v.expression("frame * 2");

    CHECK(v.evaluate(7) == doctest::Approx(14.0f));
}

TEST_CASE("AnimatedValue expression receives time variable using fps") {
    AnimatedValue<f32> v{0.0f};
    v.expression("time * 100");

    AnimationEvalContext ctx;
    ctx.frame.frame.fps = 25.0f;

    CHECK(v.evaluate(25, ctx) == doctest::Approx(100.0f));
}

TEST_CASE("AnimatedValue expression combines keyframed base value") {
    AnimatedValue<f32> v{0.0f};
    v.key(0, 0.0f).key(10, 100.0f);
    v.expression("value + 10");

    CHECK(v.evaluate(5) == doctest::Approx(60.0f));
}

TEST_CASE("AnimatedValue expression falls back to base value on parser error") {
    AnimatedValue<f32> v{42.0f};
    v.expression("unknown_var + 1");

    CHECK(v.evaluate(0) == doctest::Approx(42.0f));
}

TEST_CASE("AnimatedValue expression does not affect non-f32 values in V1") {
    AnimatedValue<Vec3> v{Vec3{10.0f, 20.0f, 30.0f}};
    v.expression("frame * 2");

    auto out = v.evaluate(7);
    CHECK(out.x == doctest::Approx(10.0f));
    CHECK(out.y == doctest::Approx(20.0f));
    CHECK(out.z == doctest::Approx(30.0f));
}
