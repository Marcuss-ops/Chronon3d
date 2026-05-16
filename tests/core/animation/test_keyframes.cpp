#include <doctest/doctest.h>
#include <chronon3d/animation/keyframe.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
TEST_CASE("keyframes: before first holds first value") {
    CHECK(keyframes(-10, {KF{0, 100.0f}, KF{60, 200.0f}}) == doctest::Approx(100.0f));
}

TEST_CASE("keyframes: after last holds last value") {
    CHECK(keyframes(100, {KF{0, 100.0f}, KF{60, 200.0f}}) == doctest::Approx(200.0f));
}

TEST_CASE("keyframes: exact first keyframe returns first value") {
    CHECK(keyframes(0, {KF{0, 100.0f}, KF{60, 200.0f}}) == doctest::Approx(100.0f));
}

TEST_CASE("keyframes: exact last keyframe returns last value") {
    CHECK(keyframes(60, {KF{0, 100.0f}, KF{60, 200.0f}}) == doctest::Approx(200.0f));
}

TEST_CASE("keyframes: midpoint linear interpolation") {
    CHECK(keyframes(30, {KF{0, 100.0f}, KF{60, 200.0f}}) == doctest::Approx(150.0f));
}

TEST_CASE("keyframes: hold segment") {
    CHECK(keyframes(45, {KF{0, 0.0f}, KF{30, 400.0f}, KF{60, 400.0f}, KF{90, 800.0f}}) ==
          doctest::Approx(400.0f));
}

TEST_CASE("keyframes: multi-segment picks correct segment") {
    // segment [30,90]: frame 60 = t=0.5 → (600-0)*0.5 = 300
    CHECK(keyframes(60, {KF{0, 0.0f}, KF{30, 0.0f}, KF{90, 600.0f}}) ==
          doctest::Approx(300.0f));
}

TEST_CASE("keyframes: single keyframe always returns its value") {
    CHECK(keyframes(-5,  {KF{0, 42.0f}}) == doctest::Approx(42.0f));
    CHECK(keyframes(0,   {KF{0, 42.0f}}) == doctest::Approx(42.0f));
    CHECK(keyframes(100, {KF{0, 42.0f}}) == doctest::Approx(42.0f));
}

TEST_CASE("keyframes: InQuad easing applied correctly") {
    // InQuad at t=0.5: t*t = 0.25 → 0 + 100*0.25 = 25
    const f32 v = keyframes(30, {KF{0, 0.0f, Easing::InQuad}, KF{60, 100.0f}});
    CHECK(v == doctest::Approx(25.0f).epsilon(0.01f));
}

TEST_CASE("keyframes: OutQuad easing applied correctly") {
    // OutQuad at t=0.5: t*(2-t) = 0.75 → 75
    const f32 v = keyframes(30, {KF{0, 0.0f, Easing::OutQuad}, KF{60, 100.0f}});
    CHECK(v == doctest::Approx(75.0f).epsilon(0.01f));
}

TEST_CASE("keyframes: different easings per segment") {
    // seg 0→30 Linear: frame 15 = t=0.5 → 50
    // seg 30→60 InQuad: frame 45 = t=0.5 → 100 + 100*0.25 = 125
    const f32 v1 = keyframes(15, {KF{0, 0.0f, Easing::Linear}, KF{30, 100.0f, Easing::InQuad}, KF{60, 200.0f}});
    const f32 v2 = keyframes(45, {KF{0, 0.0f, Easing::Linear}, KF{30, 100.0f, Easing::InQuad}, KF{60, 200.0f}});
    CHECK(v1 == doctest::Approx(50.0f).epsilon(0.01f));
    CHECK(v2 == doctest::Approx(125.0f).epsilon(0.01f));
}

TEST_CASE("KF alias: 2-arg construction defaults to Linear") {
    KF kf{0, 42.0f};
    CHECK(kf.frame  == 0);
    CHECK(kf.value  == doctest::Approx(42.0f));
    CHECK(kf.easing == Easing::Linear);
}
