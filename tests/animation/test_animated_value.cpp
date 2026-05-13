#include <doctest/doctest.h>
#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/vec3.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// value_at / evaluate alias
// ---------------------------------------------------------------------------
TEST_CASE("AnimatedValue: value_at is alias for evaluate") {
    AnimatedValue<f32> v(42.0f);
    CHECK(v.value_at(0)   == doctest::Approx(v.evaluate(0)));
    CHECK(v.value_at(100) == doctest::Approx(v.evaluate(100)));

    v.add_keyframe(0, 0.0f);
    v.add_keyframe(60, 60.0f);
    CHECK(v.value_at(30) == doctest::Approx(v.evaluate(30)));
}

// ---------------------------------------------------------------------------
// .key() fluent chaining
// ---------------------------------------------------------------------------
TEST_CASE("AnimatedValue: key() enables chaining") {
    AnimatedValue<f32> v(0.0f);
    v.key(0, 0.0f).key(60, 120.0f);

    CHECK(v.value_at(0)  == doctest::Approx(0.0f));
    CHECK(v.value_at(30) == doctest::Approx(60.0f));
    CHECK(v.value_at(60) == doctest::Approx(120.0f));
}

TEST_CASE("AnimatedValue: key() with easing") {
    AnimatedValue<f32> v(0.0f);
    v.key(0, 0.0f, Easing::OutQuad).key(60, 100.0f);
    // OutQuad at t=0.5: t*(2-t) = 0.75 → 75
    CHECK(v.value_at(30) == doctest::Approx(75.0f).epsilon(0.01f));
}

// ---------------------------------------------------------------------------
// .set() fluent + clears keyframes
// ---------------------------------------------------------------------------
TEST_CASE("AnimatedValue: set() returns *this and clears keys") {
    AnimatedValue<f32> v(0.0f);
    v.key(0, 10.0f).key(60, 100.0f);
    CHECK(v.is_animated());

    v.set(42.0f);
    CHECK_FALSE(v.is_animated());
    CHECK(v.value_at(0)   == doctest::Approx(42.0f));
    CHECK(v.value_at(999) == doctest::Approx(42.0f));
}

// ---------------------------------------------------------------------------
// lerp<Color> via Color::operator-
// ---------------------------------------------------------------------------
TEST_CASE("AnimatedValue<Color>: interpolates correctly") {
    AnimatedValue<Color> v(Color::black());
    v.key(0, Color::black()).key(60, Color::white());

    Color mid = v.value_at(30);
    CHECK(mid.r == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(mid.g == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(mid.b == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(mid.a == doctest::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("AnimatedValue<Color>: constant color returns same value") {
    AnimatedValue<Color> v(Color{0.5f, 0.2f, 0.8f, 1.0f});
    Color c = v.value_at(99);
    CHECK(c.r == doctest::Approx(0.5f));
    CHECK(c.g == doctest::Approx(0.2f));
    CHECK(c.b == doctest::Approx(0.8f));
}

// ---------------------------------------------------------------------------
// Vec3 lerp
// ---------------------------------------------------------------------------
TEST_CASE("AnimatedValue<Vec3>: interpolates component-wise") {
    AnimatedValue<Vec3> v(Vec3{0,0,0});
    v.key(0, Vec3{0,0,0}).key(60, Vec3{60, 120, 180});

    Vec3 mid = v.value_at(30);
    CHECK(mid.x == doctest::Approx(30.0f));
    CHECK(mid.y == doctest::Approx(60.0f));
    CHECK(mid.z == doctest::Approx(90.0f));
}

// ---------------------------------------------------------------------------
// Loop modes
// ---------------------------------------------------------------------------
TEST_CASE("AnimatedValue: Loop mode") {
    AnimatedValue<f32> v(0.0f);
    v.key(0, 0.0f).key(60, 60.0f);
    v.loop_mode(LoopMode::Loop);

    CHECK(v.value_at(0)   == doctest::Approx(0.0f));
    CHECK(v.value_at(30)  == doctest::Approx(30.0f));
    CHECK(v.value_at(60)  == doctest::Approx(0.0f)); // Loops back to start
    CHECK(v.value_at(90)  == doctest::Approx(30.0f));
    CHECK(v.value_at(-30) == doctest::Approx(30.0f));
}

TEST_CASE("AnimatedValue: PingPong mode") {
    AnimatedValue<f32> v(0.0f);
    v.key(0, 0.0f).key(60, 60.0f);
    v.loop_mode(LoopMode::PingPong);

    CHECK(v.value_at(0)   == doctest::Approx(0.0f));
    CHECK(v.value_at(30)  == doctest::Approx(30.0f));
    CHECK(v.value_at(60)  == doctest::Approx(60.0f));
    CHECK(v.value_at(90)  == doctest::Approx(30.0f)); // Bouncing back
    CHECK(v.value_at(120) == doctest::Approx(0.0f));  // Back to start
    CHECK(v.value_at(-30) == doctest::Approx(30.0f)); // Bouncing back from negative
}
