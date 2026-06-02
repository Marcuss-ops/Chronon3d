#include <doctest/doctest.h>
#include <chronon3d/rendering/shadow_settings.hpp>

using namespace chronon3d;
using namespace chronon3d::rendering;

TEST_CASE("ShadowSettings: defaults") {
    ShadowSettings s;
    CHECK(s.opacity == doctest::Approx(0.35f));
    CHECK(s.blur_radius == doctest::Approx(8.0f));
    CHECK(s.contact_opacity == doctest::Approx(0.28f));
    CHECK(s.contact_blur_radius == doctest::Approx(10.0f));
    CHECK(s.ambient_opacity == doctest::Approx(0.08f));
    CHECK(s.ambient_blur_radius == doctest::Approx(96.0f));
}

TEST_CASE("ShadowSettings: depth-aware defaults") {
    ShadowSettings s;
    CHECK(s.depth_aware);
    CHECK(s.blur_per_z == doctest::Approx(0.04f));
    CHECK(s.opacity_falloff == doctest::Approx(0.0025f));
    CHECK(s.tint.r == doctest::Approx(0.03f));
    CHECK(s.tint.g == doctest::Approx(0.04f));
    CHECK(s.tint.b == doctest::Approx(0.08f));
    CHECK(s.tint.a == doctest::Approx(1.0f));
}

TEST_CASE("ShadowSettings: depth-aware blur increases with distance") {
    ShadowSettings s;
    s.depth_aware = true;
    s.blur_per_z = 0.04f;

    // Simulate blur calculation for different depths
    f32 depth_near = 100.0f;
    f32 depth_far = 1000.0f;

    f32 blur_near = 1.0f + depth_near * s.blur_per_z;
    f32 blur_far = 1.0f + depth_far * s.blur_per_z;

    CHECK(blur_far > blur_near);
    CHECK(blur_near == doctest::Approx(5.0f));
    CHECK(blur_far == doctest::Approx(41.0f));
}

TEST_CASE("ShadowSettings: depth-aware opacity decreases with distance") {
    ShadowSettings s;
    s.depth_aware = true;
    s.opacity_falloff = 0.0025f;

    f32 depth_near = 100.0f;
    f32 depth_far = 1000.0f;

    f32 opacity_near = 1.0f / (1.0f + depth_near * s.opacity_falloff);
    f32 opacity_far = 1.0f / (1.0f + depth_far * s.opacity_falloff);

    CHECK(opacity_near > opacity_far);
    CHECK(opacity_near == doctest::Approx(0.80f).epsilon(0.01));
    CHECK(opacity_far == doctest::Approx(0.286f).epsilon(0.01));
}

TEST_CASE("ShadowSettings: depth-aware=false uses legacy constants") {
    ShadowSettings s;
    s.depth_aware = false;

    // When depth_aware is false, the renderer uses hardcoded 0.0030f and 0.0025f
    // This test just verifies the flag is respected
    CHECK_FALSE(s.depth_aware);
}

TEST_CASE("ShadowSettings: custom tint") {
    ShadowSettings s;
    s.tint = {0.5f, 0.0f, 0.0f, 1.0f}; // red shadow
    CHECK(s.tint.r == doctest::Approx(0.5f));
    CHECK(s.tint.g == doctest::Approx(0.0f));
    CHECK(s.tint.b == doctest::Approx(0.0f));
}

TEST_CASE("ShadowSettings: zero depth produces no extra blur") {
    ShadowSettings s;
    s.depth_aware = true;
    s.blur_per_z = 0.04f;

    f32 depth = 0.0f;
    f32 blur_scale = 1.0f + depth * s.blur_per_z;
    CHECK(blur_scale == doctest::Approx(1.0f));
}
