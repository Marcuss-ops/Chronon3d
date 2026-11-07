#include <doctest/doctest.h>

#include <chronon3d/text/gel_surface.hpp>

#include <cmath>

TEST_CASE("gel surface math stays finite around thin and intersecting SDF features") {
    using namespace chronon3d;
    using namespace chronon3d::text::gel;

    for (float distance = 0.0f; distance <= 16.0f; distance += 0.125f) {
        const auto sample = reconstruct_surface(
            distance, Vec2{0.70710678f, -0.70710678f}, 16.0f, 1.5f);
        CHECK(std::isfinite(sample.height));
        CHECK(std::isfinite(sample.normal.x));
        CHECK(std::isfinite(sample.normal.y));
        CHECK(std::isfinite(sample.normal.z));
        CHECK(glm::length(sample.normal) == doctest::Approx(1.0f).epsilon(1e-5));
    }

    const float joined = smooth_union_distance(2.0f, 2.0f, 4.0f);
    CHECK(std::isfinite(joined));
    CHECK(joined > 2.0f); // Positive-inside union is smooth-max.
    CHECK(smooth_union_distance(1.0f, 3.0f, 4.0f) ==
          doctest::Approx(smooth_union_distance(3.0f, 1.0f, 4.0f)));
    CHECK(smooth_union_distance(1.0f, 3.0f, 4.0f) > 3.0f);
    CHECK(smooth_union_distance(-10000.0f, 4.0f, 4.0f) ==
          doctest::Approx(4.0f));
    CHECK(rounded_height(0.0f, 16.0f) == 0.0f);
    CHECK(rounded_height(16.0f, 16.0f) == doctest::Approx(16.0f));
    const float eps = 0.001f;
    const float left_edge_slope =
        (rounded_height(eps, 16.0f) - rounded_height(0.0f, 16.0f)) / eps;
    // Height differences at the medial axis are below float resolution for
    // this epsilon (the smootherstep residual is O(eps^3)); check the shared
    // analytic derivative instead of amplifying cancellation by division.
    const float centre_slope = rounded_slope(16.0f - eps, 16.0f);
    CHECK(left_edge_slope < 0.001f);
    CHECK(centre_slope < 0.001f);
    CHECK(rounded_height(8.0f, 16.0f) == doctest::Approx(8.0f));

    // The dome also has zero second derivative at both ends (C2 join to the
    // zero-height exterior and a flat medial-axis peak). Estimate curvature
    // from the analytic first derivative to avoid float cancellation in the
    // second difference of heights near the peak.
    const float edge_curvature =
        (rounded_slope(2.0f * eps, 16.0f) - rounded_slope(eps, 16.0f)) / eps;
    const float peak_curvature =
        (rounded_slope(16.0f, 16.0f) -
         rounded_slope(16.0f - eps, 16.0f)) / eps;
    CHECK(rounded_slope(0.0f, 16.0f) == 0.0f);
    CHECK(rounded_slope(16.0f, 16.0f) == 0.0f);
    CHECK(rounded_slope(8.0f, 16.0f) == doctest::Approx(1.875f));
    CHECK(std::abs(edge_curvature) < 0.001f);
    CHECK(std::abs(peak_curvature) < 0.001f);

    // At the profile midpoint dh/dd = 30*t^2*(1-t)^2 = 1.875. The
    // reconstructed normal must use this chain-rule slope rather than a
    // second height finite-difference pass.
    const auto midpoint = reconstruct_surface(8.0f, Vec2{1.0f, 0.0f}, 16.0f);
    CHECK(midpoint.normal.x == doctest::Approx(-15.0f / 17.0f));
    CHECK(midpoint.normal.y == doctest::Approx(0.0f));
    CHECK(midpoint.normal.z == doctest::Approx(8.0f / 17.0f));
    const auto medial_axis = reconstruct_surface(16.0f, Vec2{0.0f, 1.0f}, 16.0f);
    CHECK(medial_axis.normal.x == doctest::Approx(0.0f));
    CHECK(medial_axis.normal.y == doctest::Approx(0.0f));
    CHECK(medial_axis.normal.z == doctest::Approx(1.0f));
}

TEST_CASE("gel dome radius covers a bold stroke without a medial plateau") {
    using namespace chronon3d::text::gel;

    const float reference_em = 64.0f;
    const float reference_px = 112.0f;
    const float texel_to_screen = reference_px / reference_em;
    const float radius = dome_radius_px(texel_to_screen);

    // 5.2 canonical em-texels at the reference size: large enough to cover
    // the medial-axis distance of a bold stroke, unlike the old 3.6 constant.
    CHECK(radius == doctest::Approx(9.1f));
    CHECK(radius > 3.6f * texel_to_screen);
    CHECK(rounded_height(radius, radius) == doctest::Approx(radius));
    CHECK(rounded_height(0.8f * radius, radius) < 0.99f * radius);
    CHECK(dome_radius_px(0.0f) == doctest::Approx(kMinDomeRadiusPx));
    CHECK(dome_radius_px(1000.0f) == doctest::Approx(kMaxDomeRadiusPx));
}

TEST_CASE("gel light parameters pack full azimuth and elevation into stable UNORM16 lanes") {
    using namespace chronon3d::text::gel;
    const auto left_horizon = pack_light_parameters(-3.14159265f, 0.0f);
    const auto zenith = pack_light_parameters(0.0f, 1.0f);
    const auto right_horizon = pack_light_parameters(3.14159265f, 0.0f);
    CHECK(left_horizon != zenith);
    CHECK(right_horizon != zenith);
    CHECK(left_horizon != right_horizon);
    CHECK((left_horizon >> 16u) == 0u);
    CHECK((zenith >> 16u) == 65535u);
    CHECK((right_horizon >> 16u) == 0u);
    CHECK(pack_light_parameters(0.0f, 0.5f) !=
          pack_light_parameters(1.5707963f, 0.5f));
}

TEST_CASE("gel surface geometric roughness damps subpixel specular variation") {
    using namespace chronon3d;
    using namespace chronon3d::text::gel;

    const float base = geometric_roughness(0.04f, Vec3{}, Vec3{});
    const float high_variance = geometric_roughness(
        0.04f, Vec3{0.0f, 0.8f, 0.0f}, Vec3{0.8f, 0.0f, 0.0f});
    CHECK(base == doctest::Approx(0.04f));
    CHECK(high_variance > base);
    CHECK(high_variance <= 1.0f);
    CHECK(geometric_roughness(0.04f, Vec3{NAN, 0.0f, 0.0f}, Vec3{}) ==
          doctest::Approx(0.04f));
}

TEST_CASE("gel surface wrap lighting has no hard grazing terminator") {
    using namespace chronon3d::text::gel;

    const float before = half_lambert(-0.001f);
    const float at = half_lambert(0.0f);
    const float after = half_lambert(0.001f);
    CHECK(before > 0.0f);
    CHECK(before < at);
    CHECK(at < after);
    CHECK(after - before < 0.01f);
}

TEST_CASE("gel surface Beer-Lambert transmission preserves background at zero thickness") {
    using namespace chronon3d;
    using namespace chronon3d::text::gel;

    const Color background{0.2f, 0.7f, 0.9f, 1.0f};
    const Color gel_color{0.0f, 0.8f, 1.0f, 1.0f};
    const Color absorption{1.8f, 0.3f, 0.05f, 0.0f};
    const auto empty = transmit(background, gel_color, absorption, 0.0f);
    CHECK(empty.r == doctest::Approx(background.r));
    CHECK(empty.g == doctest::Approx(background.g));
    CHECK(empty.b == doctest::Approx(background.b));
    const auto body = transmit(background, gel_color, absorption, 1.0f);
    CHECK(body.r < background.r);
    CHECK(body.b > background.b);
    CHECK(body.a == doctest::Approx(background.a));
}

TEST_CASE("gel body Beer-Lambert absorption darkens the dome more than its rim") {
    using namespace chronon3d;
    using namespace chronon3d::text::gel;

    const Color cyan{0.25f, 0.9f, 1.0f, 0.75f};
    const Color sigma{0.45f, 0.12f, 0.02f, 0.0f};
    const auto rim = absorb(cyan, sigma, 0.0f);
    const auto body = absorb(cyan, sigma, 1.0f);
    CHECK(rim.r == doctest::Approx(cyan.r));
    CHECK(rim.g == doctest::Approx(cyan.g));
    CHECK(rim.b == doctest::Approx(cyan.b));
    CHECK(body.r < body.g);
    CHECK(body.g < body.b);
    CHECK(body.a == doctest::Approx(cyan.a));
}

TEST_CASE("gel rim keeps a reflective contour with a C2 dome profile") {
    using namespace chronon3d::text::gel;

    CHECK(rim_response(1.0f, 0.0f, 3.0f) == doctest::Approx(0.78f));
    CHECK(rim_response(1.0f, 1.5f, 3.0f) > 0.0f);
    CHECK(rim_response(1.0f, 3.0f, 3.0f) == doctest::Approx(0.0f));
    CHECK(rim_response(0.0f, 3.0f, 3.0f) == doctest::Approx(1.0f));
    CHECK(std::isfinite(rim_response(NAN, INFINITY, 3.0f)));
}

TEST_CASE("gel surface reference parameters remain bounded under animated scaling") {
    using namespace chronon3d;
    using namespace chronon3d::text::gel;

    for (float scale = 0.05f; scale <= 10.0f; scale *= 1.35f) {
        const float radius = std::max(0.25f, 16.0f * scale);
        const auto sample = reconstruct_surface(
            radius * 0.5f, Vec2{1.0f / scale, 0.25f / scale}, radius, scale);
        const float roughness = geometric_roughness(
            0.04f, Vec3{sample.normal.x, 0.0f, 0.0f},
            Vec3{0.0f, sample.normal.y, 0.0f});
        CHECK(std::isfinite(roughness));
        CHECK(roughness >= 0.04f);
        CHECK(roughness <= 1.0f);
    }
}
