#include <doctest/doctest.h>
#include <chronon3d/rendering/depth_grade.hpp>
using namespace chronon3d;

using namespace chronon3d::rendering;

TEST_CASE("DepthGrade: defaults") {
    DepthGrade g;
    CHECK(g.enabled);
    CHECK(g.near_z == doctest::Approx(300.0f));
    CHECK(g.far_z == doctest::Approx(1800.0f));
    CHECK(g.fog_opacity == doctest::Approx(0.35f));
    CHECK(g.far_saturation == doctest::Approx(0.65f));
    CHECK(g.far_contrast == doctest::Approx(0.75f));
    CHECK(g.far_blur_px == doctest::Approx(4.0f));
}

TEST_CASE("DepthGrade: normalized_depth at near_z returns ~0") {
    DepthGrade g;
    f32 t = g.normalized_depth(300.0f);
    CHECK(t == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("DepthGrade: normalized_depth at far_z returns ~1") {
    DepthGrade g;
    f32 t = g.normalized_depth(1800.0f);
    CHECK(t == doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("DepthGrade: normalized_depth beyond far_z clamps to 1") {
    DepthGrade g;
    f32 t = g.normalized_depth(5000.0f);
    CHECK(t == doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("DepthGrade: normalized_depth before near_z returns 0") {
    DepthGrade g;
    f32 t = g.normalized_depth(50.0f);
    CHECK(t == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("DepthGrade: normalized_depth is monotonically increasing") {
    DepthGrade g;
    f32 prev = 0.0f;
    for (f32 z = 0.0f; z <= 2000.0f; z += 50.0f) {
        f32 t = g.normalized_depth(z);
        CHECK(t >= prev);
        prev = t;
    }
}

TEST_CASE("DepthGrade: apply at t=0 returns unchanged color") {
    DepthGrade g;
    Color base{0.8f, 0.4f, 0.2f, 1.0f};
    Color result = g.apply(base, 0.0f);
    CHECK(result.r == doctest::Approx(0.8f));
    CHECK(result.g == doctest::Approx(0.4f));
    CHECK(result.b == doctest::Approx(0.2f));
}

TEST_CASE("DepthGrade: apply at t=1 mixes fog and desaturates") {
    DepthGrade g;
    Color base{1.0f, 0.0f, 0.0f, 1.0f}; // pure red
    Color result = g.apply(base, 1.0f);
    // Red should be reduced toward fog color
    CHECK(result.r < 1.0f);
    // Blue channel should increase (fog has blue component)
    CHECK(result.b > 0.0f);
    // Alpha should be preserved
    CHECK(result.a == doctest::Approx(1.0f));
}

TEST_CASE("DepthGrade: disabled returns base unchanged") {
    DepthGrade g;
    g.enabled = false;
    Color base{0.5f, 0.3f, 0.8f, 0.9f};
    Color result = g.apply(base, 0.5f);
    CHECK(result.r == doctest::Approx(0.5f));
    CHECK(result.g == doctest::Approx(0.3f));
    CHECK(result.b == doctest::Approx(0.8f));
}

TEST_CASE("DepthGrade: blur_for_depth returns proportional value") {
    DepthGrade g;
    CHECK(g.blur_for_depth(0.0f) == doctest::Approx(0.0f));
    CHECK(g.blur_for_depth(0.5f) == doctest::Approx(2.0f));
    CHECK(g.blur_for_depth(1.0f) == doctest::Approx(4.0f));
}

TEST_CASE("DepthGrade: smoothstep is smooth") {
    // Smoothstep should be 0 at edge0, 1 at edge1, and smooth in between
    CHECK(DepthGrade::smoothstep(0.0f, 1.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(DepthGrade::smoothstep(0.0f, 1.0f, 1.0f) == doctest::Approx(1.0f));
    f32 mid = DepthGrade::smoothstep(0.0f, 1.0f, 0.5f);
    CHECK(mid == doctest::Approx(0.5f));
}

TEST_CASE("DepthGrade: atmospheric preset") {
    auto g = DepthGrade::atmospheric();
    CHECK(g.enabled);
    CHECK(g.near_z == doctest::Approx(200.0f));
    CHECK(g.far_z == doctest::Approx(1600.0f));
    CHECK(g.fog_opacity == doctest::Approx(0.25f));
    CHECK(g.far_saturation == doctest::Approx(0.70f));
}

TEST_CASE("DepthGrade: deep_depth preset") {
    auto g = DepthGrade::deep_depth();
    CHECK(g.enabled);
    CHECK(g.far_z == doctest::Approx(2000.0f));
    CHECK(g.fog_opacity == doctest::Approx(0.45f));
    CHECK(g.far_saturation == doctest::Approx(0.50f));
    CHECK(g.far_contrast == doctest::Approx(0.65f));
}

TEST_CASE("DepthGrade: subtle preset") {
    auto g = DepthGrade::subtle();
    CHECK(g.enabled);
    CHECK(g.fog_opacity == doctest::Approx(0.15f));
    CHECK(g.far_saturation == doctest::Approx(0.85f));
}

TEST_CASE("DepthGrade: apply preserves order (farther = more graded)") {
    DepthGrade g;
    Color base{0.8f, 0.6f, 0.4f, 1.0f};
    Color near_result = g.apply(base, 0.1f);
    Color mid_result = g.apply(base, 0.5f);
    Color far_result = g.apply(base, 1.0f);

    // Saturation should decrease with depth
    // (farther pixels should be more desaturated)
    f32 near_range = std::abs(near_result.r - near_result.b);
    f32 far_range = std::abs(far_result.r - far_result.b);
    CHECK(near_range >= far_range);
}
