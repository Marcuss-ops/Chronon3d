// Single-glow API and pipeline contract tests.
#include <doctest/doctest.h>

#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/effects/spatial_bounds.hpp>

using namespace chronon3d;

TEST_CASE("GlowPipeline converts the single radius, intensity and color") {
    GlowParams params{
        .radius = 24.0f,
        .intensity = 0.35f,
        .color = Color{0.5f, 0.6f, 0.7f, 1.0f},
    };

    const auto pipeline = GlowPipeline::from(params);
    CHECK(pipeline.color.r == doctest::Approx(0.5f));
    CHECK(pipeline.color.g == doctest::Approx(0.6f));
    CHECK(pipeline.color.b == doctest::Approx(0.7f));
    CHECK(pipeline.radius == doctest::Approx(24.0f));
    CHECK(pipeline.intensity == doctest::Approx(0.35f));
}

TEST_CASE("glow_effect_extent follows the single radius") {
    GlowParams params;
    params.radius = 20.0f;
    CHECK(glow_effect_extent(params) == doctest::Approx(84.0f));

    const auto pipeline = GlowPipeline::from(params);
    CHECK(glow_effect_extent(pipeline) == doctest::Approx(84.0f));
}

TEST_CASE("effect_spatial_halo matches software blur strategies and glow bounds") {
    const auto blur_halo = effects::effect_spatial_halo(BlurParams{.radius = 10.0f});
    CHECK(blur_halo.left == doctest::Approx(26.0f));
    CHECK(blur_halo.top == doctest::Approx(26.0f));
    CHECK(blur_halo.right == doctest::Approx(26.0f));
    CHECK(blur_halo.bottom == doctest::Approx(26.0f));

    // These radii select the half-resolution, quarter-resolution, and nested
    // quarter-resolution blur branches in apply_blur(). The halo must include
    // the kernel support and resampling fringe at every level.
    CHECK(effects::spatial_halo_extent(
              effects::effect_spatial_halo(BlurParams{.radius = 20.0f})) >= 22.0f);
    CHECK(effects::spatial_halo_extent(
              effects::effect_spatial_halo(BlurParams{.radius = 30.0f})) >= 34.0f);
    CHECK(effects::spatial_halo_extent(
              effects::effect_spatial_halo(BlurParams{.radius = 100.0f})) >= 110.0f);

    // The renderer clamps pass count to one; the spatial contract must do so
    // as well instead of returning a zero halo for caller values <= 0.
    const auto zero_pass_halo = effects::blur_spatial_halo(8.0f, 0);
    const auto one_pass_halo = effects::blur_spatial_halo(8.0f, 1);
    CHECK(zero_pass_halo.left == doctest::Approx(one_pass_halo.left));
    CHECK(zero_pass_halo.right == doctest::Approx(one_pass_halo.right));
    CHECK(effects::spatial_halo_extent(
              effects::blur_spatial_halo(8.0f, -1)) > 0.0f);

    const auto glow_halo = effects::effect_spatial_halo(GlowParams{.radius = 20.0f});
    CHECK(glow_halo.left == doctest::Approx(84.0f));
    CHECK(glow_halo.top == doctest::Approx(84.0f));
    CHECK(glow_halo.right == doctest::Approx(84.0f));
    CHECK(glow_halo.bottom == doctest::Approx(84.0f));
}

TEST_CASE("PR2-Unit-ShadowBounds: halo matches contact and ambient shadow maps") {
    DropShadowParams params;
    params.offset = {0.0f, 4.0f};
    params.radius = 12.0f;

    const auto halo = effects::drop_shadow_spatial_halo(params);
    CHECK(halo.left == doctest::Approx(44.0f));
    CHECK(halo.top == doctest::Approx(44.0f));
    CHECK(halo.right == doctest::Approx(44.0f));
    CHECK(halo.bottom == doctest::Approx(50.6f));
    CHECK(effects::drop_shadow_blur_padding(params) == doctest::Approx(44.0f));

    // Both maps are included in the shared ROI even for atypical negative
    // radius values where contact blur can be the wider kernel.
    params.radius = -10.0f;
    const auto negative_radius_halo = effects::effect_spatial_halo(params);
    CHECK(effects::spatial_halo_extent(negative_radius_halo) >=
          effects::spatial_halo_extent(effects::blur_spatial_halo(1.0f, 1)));

    params.radius = 12.0f;
    params.offset = {-7.0f, -11.0f};
    const auto negative_offset_halo = effects::effect_spatial_halo(params);
    CHECK(negative_offset_halo.left == doctest::Approx(56.0f));
    CHECK(negative_offset_halo.top == doctest::Approx(62.0f));
    CHECK(negative_offset_halo.right == doctest::Approx(44.0f));
    CHECK(negative_offset_halo.bottom == doctest::Approx(44.0f));

    // Match the implementation's integer pixel offsets: the 0.6px authored
    // offset rounds to one contact pixel and two ambient pixels.
    params.offset = {0.6f, -0.4f};
    const auto fractional_offset_halo = effects::effect_spatial_halo(params);
    CHECK(fractional_offset_halo.right == doctest::Approx(46.0f));
    CHECK(fractional_offset_halo.left == doctest::Approx(44.0f));
    CHECK(fractional_offset_halo.top == doctest::Approx(44.0f));
    CHECK(fractional_offset_halo.bottom == doctest::Approx(44.0f));
}
