#include <doctest/doctest.h>
#include <chronon3d/animation/keyframe.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/color.hpp>

using namespace chronon3d;

TEST_CASE("KeyframeTrack Basic Interpolation") {
    auto track = keyframes<f32>({
        {0,  0.0f,  Easing::Linear},
        {60, 100.0f, Easing::Linear}
    });

    CHECK(track.value(-10) == doctest::Approx(0.0f));
    CHECK(track.value(0)   == doctest::Approx(0.0f));
    CHECK(track.value(30)  == doctest::Approx(50.0f));
    CHECK(track.value(60)  == doctest::Approx(100.0f));
    CHECK(track.value(70)  == doctest::Approx(100.0f));
}

TEST_CASE("KeyframeTrack Hold Easing") {
    auto track = keyframes<f32>({
        {0,  10.0f, Easing::Hold},
        {30, 20.0f, Easing::Hold},
        {60, 30.0f, Easing::Hold}
    });

    CHECK(track.value(0)  == doctest::Approx(10.0f));
    CHECK(track.value(15) == doctest::Approx(10.0f));
    CHECK(track.value(29) == doctest::Approx(10.0f));
    CHECK(track.value(30) == doctest::Approx(20.0f));
    CHECK(track.value(45) == doctest::Approx(20.0f));
    CHECK(track.value(60) == doctest::Approx(30.0f));
}

TEST_CASE("KeyframeTrack Vec3 Interpolation") {
    auto track = keyframes<Vec3>({
        {0,  {0, 0, 0}},
        {100, {100, 200, 300}}
    });

    Vec3 v = track.value(50);
    CHECK(v.x == doctest::Approx(50.0f));
    CHECK(v.y == doctest::Approx(100.0f));
    CHECK(v.z == doctest::Approx(150.0f));
}

TEST_CASE("KeyframeTrack Color Interpolation") {
    auto track = keyframes<Color>({
        {0,   {1, 0, 0, 1}}, // Red
        {100, {0, 0, 1, 1}}  // Blue
    });

    Color c = track.value(50);
    CHECK(c.r == doctest::Approx(0.5f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.5f));
    CHECK(c.a == doctest::Approx(1.0f));
}

TEST_CASE("KeyframeTrack New API Syntax") {
    // Verifying the user-requested syntax
    auto x = keyframes<f32>({
        {0,  -300.0f, Easing::OutCubic},
        {40, 0.0f,    Easing::OutBack},
        {90, 120.0f,  Easing::InOutSine}
    }).value(40);
    
    CHECK(x == doctest::Approx(0.0f));
}

TEST_CASE("KeyframeTrack Legacy Compatibility") {
    // Verifying the old API still works
    f32 v = keyframes(30, { KF{0, 0.0f}, KF{60, 100.0f} });
    CHECK(v == doctest::Approx(50.0f));
}
