#include <doctest/doctest.h>
#include <chronon3d/animation/effects/wiggle.hpp>
#include <cmath>
using namespace chronon3d;


// ── hash_noise tests ─────────────────────────────────────────────────────────

TEST_CASE("hash_noise: deterministic for same input") {
    f32 a = chronon3d::detail::hash_noise(42, 0);
    f32 b = chronon3d::detail::hash_noise(42, 0);
    CHECK(a == doctest::Approx(b));
}

TEST_CASE("hash_noise: different for different input") {
    f32 a = chronon3d::detail::hash_noise(1, 0);
    f32 b = chronon3d::detail::hash_noise(2, 0);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("hash_noise: different seeds produce different values") {
    f32 a = chronon3d::detail::hash_noise(1, 0);
    f32 b = chronon3d::detail::hash_noise(1, 100);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("hash_noise: output range [-1, 1]") {
    for (int i = 0; i < 100; ++i) {
        f32 v = chronon3d::detail::hash_noise(i, 7);
        CHECK(v >= -1.0f);
        CHECK(v <= 1.0f);
    }
}

// ── smooth_noise tests ───────────────────────────────────────────────────────

TEST_CASE("smooth_noise: deterministic") {
    f32 a = chronon3d::detail::smooth_noise(1.5f, 0);
    f32 b = chronon3d::detail::smooth_noise(1.5f, 0);
    CHECK(a == doctest::Approx(b));
}

TEST_CASE("smooth_noise: continuous (no discontinuities at integers)") {
    f32 left = chronon3d::detail::smooth_noise(4.999f, 0);
    f32 right = chronon3d::detail::smooth_noise(5.001f, 0);
    CHECK(std::abs(left - right) < 0.5f);  // smoothstep ensures continuity
}

// ── wiggle() scalar tests ────────────────────────────────────────────────────

TEST_CASE("wiggle: zero amplitude returns zero") {
    CHECK(wiggle(3.0f, 0.0f, 10.0f) == doctest::Approx(0.0f));
}

TEST_CASE("wiggle: zero frequency returns zero") {
    CHECK(wiggle(0.0f, 15.0f, 10.0f) == doctest::Approx(0.0f));
}

TEST_CASE("wiggle: output bounded by amplitude") {
    for (int i = 0; i < 200; ++i) {
        f32 v = wiggle(3.0f, 15.0f, static_cast<f32>(i) * 0.1f);
        CHECK(v >= -15.0f);
        CHECK(v <= 15.0f);
    }
}

TEST_CASE("wiggle: deterministic for same input") {
    f32 a = wiggle(3.0f, 15.0f, 42.0f, 0);
    f32 b = wiggle(3.0f, 15.0f, 42.0f, 0);
    CHECK(a == doctest::Approx(b));
}

TEST_CASE("wiggle: different seeds produce different values") {
    f32 a = wiggle(3.0f, 15.0f, 42.0f, 0);
    f32 b = wiggle(3.0f, 15.0f, 42.0f, 100);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("wiggle: intensity scales with amplitude") {
    f32 small = wiggle(3.0f, 5.0f, 20.0f);
    f32 large = wiggle(3.0f, 50.0f, 20.0f);
    CHECK(std::abs(large) > std::abs(small));
}

TEST_CASE("wiggle: Frame overload works") {
    f32 a = wiggle(3.0f, 15.0f, Frame{42}, 0);
    f32 b = wiggle(3.0f, 15.0f, 42.0f, 0);
    CHECK(a == doctest::Approx(b));
}

// ── wiggle3D() tests ─────────────────────────────────────────────────────────

TEST_CASE("wiggle3D: zero amplitude returns zero vector") {
    Vec3 v = wiggle3D(3.0f, 0.0f, 10.0f);
    CHECK(v.x == doctest::Approx(0.0f));
    CHECK(v.y == doctest::Approx(0.0f));
    CHECK(v.z == doctest::Approx(0.0f));
}

TEST_CASE("wiggle3D: components are different (decorrelated)") {
    Vec3 v = wiggle3D(3.0f, 15.0f, 42.0f);
    // Different seeds per axis should produce different values
    CHECK(v.x != doctest::Approx(v.y));
    CHECK(v.y != doctest::Approx(v.z));
}

TEST_CASE("wiggle3D: bounded by amplitude") {
    for (int i = 0; i < 100; ++i) {
        Vec3 v = wiggle3D(3.0f, 15.0f, static_cast<f32>(i) * 0.5f);
        CHECK(v.x >= -15.0f); CHECK(v.x <= 15.0f);
        CHECK(v.y >= -15.0f); CHECK(v.y <= 15.0f);
        CHECK(v.z >= -15.0f); CHECK(v.z <= 15.0f);
    }
}

TEST_CASE("wiggle3D: per-axis amplitude") {
    Vec3 amp{10.0f, 5.0f, 20.0f};
    Vec3 v = wiggle3D(3.0f, amp, 42.0f);
    CHECK(v.x >= -10.0f); CHECK(v.x <= 10.0f);
    CHECK(v.y >= -5.0f);  CHECK(v.y <= 5.0f);
    CHECK(v.z >= -20.0f); CHECK(v.z <= 20.0f);
}
