#include <doctest/doctest.h>
#include <chronon3d/animation/effects/animated_transform.hpp>

using namespace chronon3d;

TEST_CASE("AnimatedTransform: evaluate position at frame") {
    AnimatedTransform at;
    at.position.key(0, Vec3{0, 0, 0}).key(60, Vec3{120, 0, 0});
    Transform t = at.evaluate(30);
    CHECK(t.position.x == doctest::Approx(60.0f));
    CHECK(t.position.y == doctest::Approx(0.0f));
}

TEST_CASE("AnimatedTransform: evaluate opacity") {
    AnimatedTransform at;
    at.opacity.key(0, 0.0f).key(30, 1.0f);
    CHECK(at.evaluate(0).opacity  == doctest::Approx(0.0f));
    CHECK(at.evaluate(15).opacity == doctest::Approx(0.5f));
    CHECK(at.evaluate(30).opacity == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedTransform: evaluate anchor") {
    AnimatedTransform at;
    at.anchor.key(0, Vec3{0, 0, 0}).key(60, Vec3{100, 50, 0});
    Transform t = at.evaluate(60);
    CHECK(t.anchor.x == doctest::Approx(100.0f));
    CHECK(t.anchor.y == doctest::Approx(50.0f));
}

TEST_CASE("AnimatedTransform: is_animated true when any track has keys") {
    AnimatedTransform at;
    CHECK_FALSE(at.is_animated());
    at.opacity.key(0, 0.0f);
    CHECK(at.is_animated());
}

TEST_CASE("AnimatedTransform: static (no keys) returns defaults") {
    AnimatedTransform at;
    Transform t = at.evaluate(999);
    CHECK(t.position.x == doctest::Approx(0.0f));
    CHECK(t.opacity    == doctest::Approx(1.0f));
    CHECK(t.scale.x    == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedTransform: evaluate rotation_euler interpolates and produces unit quat") {
    AnimatedTransform at;
    at.rotation_euler.key(0, Vec3{0, 0, 0}).key(60, Vec3{0, 180, 0});
    Transform t = at.evaluate(30);
    // Quat must be normalized (unit length)
    float qlen = std::sqrt(t.rotation.x*t.rotation.x + t.rotation.y*t.rotation.y +
                           t.rotation.z*t.rotation.z + t.rotation.w*t.rotation.w);
    CHECK(qlen == doctest::Approx(1.0f).epsilon(0.01f));
    // At frame 30 (halfway), rotation Y should be ~90 degrees — w should not be 1
    CHECK(t.rotation.w < 0.99f);
}
