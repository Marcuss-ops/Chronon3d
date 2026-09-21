// ==============================================================================
// tests/renderer/effects/test_glow_pipeline_unit.cpp
//
// PR2 — GlowPipeline unit tests (math/API correctness).
//
// GlowPipeline is the unified struct that captures every parameter any of
// the unified glow paths (software effect processing, text_glow,
// These unit tests verify the conversions and pure-math
// helpers:
//   1. GlowPipeline::from(GlowParams) preserves all important fields.
//   2. GlowPipeline::from(BloomParams) sets Mode::Bloom.
//   3. glow_effect_extent() returns max(implied radius + 4 px).
//   4. GlowPipeline::from(GlowParams) preserves quality + the layer table.
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/effects/presets/glow_presets.hpp>
#include <chronon3d/effects/spatial_bounds.hpp>

#include <cmath>
using namespace chronon3d;

TEST_CASE("PR2-Unit-Glow: from(GlowParams) round-trips the main fields") {
    GlowParams p;
    p.color = Color{0.5f, 0.6f, 0.7f, 1.0f};
    p.radius = 24.0f;
    p.intensity = 1.25f;
    p.spread = 1.5f;
    p.core_strength = 0.7f;
    p.aura_strength = 0.4f;
    p.bloom_strength = 0.2f;

    auto pipeline = GlowPipeline::from(p);
    CHECK(pipeline.mode == GlowPipeline::Mode::Layer);
    CHECK(pipeline.color.r == doctest::Approx(0.5f));
    CHECK(pipeline.color.g == doctest::Approx(0.6f));
    CHECK(pipeline.color.b == doctest::Approx(0.7f));
    CHECK(pipeline.radius    == doctest::Approx(24.0f));
    CHECK(pipeline.intensity == doctest::Approx(1.25f));
    CHECK(pipeline.spread    == doctest::Approx(1.5f));
    CHECK(pipeline.quality == GlowQuality::Fast);

    p.quality = GlowQuality::High;
    CHECK(GlowPipeline::from(p).quality == GlowQuality::High);
}

TEST_CASE("PR2-Unit-Glow: from(BloomParams) sets Mode::Bloom + additive") {
    BloomParams p;
    p.radius = 12.0f;
    p.intensity = 0.7f;
    p.threshold = 0.85f;

    auto pipeline = GlowPipeline::from(p);
    CHECK(pipeline.mode == GlowPipeline::Mode::Bloom);
    CHECK(pipeline.preserve_source == true);
    // bloom uses source colours, not a tint palette
    CHECK(pipeline.color.r == doctest::Approx(1.0f));
    CHECK(pipeline.color.g == doctest::Approx(1.0f));
    CHECK(pipeline.color.b == doctest::Approx(1.0f));
}

TEST_CASE("PR2-Unit-Glow: glow_effect_extent accounts for max layer radius + 4 px pad") {
    GlowParams p;
    p.radius = 20.0f;
    p.spread = 1.0f;
    // Expected: radius = 20 * 1.0 = 20; the Gaussian ROI uses 4 sigma
    // plus a 4 px guard, matching the renderer's actual padding.
    const float e = glow_effect_extent(p);
    CHECK(e == doctest::Approx(84.0f));

    // Synthetic layers override radius: max layer wins.
    GlowLayer layer;
    layer.radius = 50.0f;
    p.layers.push_back(layer);
    const float e2 = glow_effect_extent(p);
    CHECK(e2 == doctest::Approx(204.0f));
}

TEST_CASE("GlowQuality: from(GlowParams) preserves quality and the layer table") {
    GlowParams params;
    params.quality = GlowQuality::Fast;
    params.layers = {
        GlowLayer{4.0f, 0.55f, 1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}},
        GlowLayer{14.0f, 0.22f, 1.0f, Color{0.50f, 0.70f, 1.0f, 1.0f}},
        GlowLayer{34.0f, 0.08f, 0.25f, Color{0.50f, 0.70f, 1.0f, 1.0f}},
    };
    params.core_strength = params.layers[0].opacity;
    params.aura_strength = params.layers[1].opacity;
    params.bloom_strength = params.layers[2].opacity;

    const auto pipeline = GlowPipeline::from(params);
    CHECK(pipeline.quality == GlowQuality::Fast);
    REQUIRE(pipeline.layers.size() == 3);
    CHECK(pipeline.layers[0].radius == doctest::Approx(4.0f));
    CHECK(pipeline.layers[1].radius == doctest::Approx(14.0f));
    CHECK(pipeline.layers[2].radius == doctest::Approx(34.0f));
    CHECK(pipeline.layers[2].scale == doctest::Approx(0.25f));
    CHECK(pipeline.core_strength == doctest::Approx(pipeline.layers[0].opacity));
    CHECK(pipeline.aura_strength == doctest::Approx(pipeline.layers[1].opacity));
    CHECK(pipeline.bloom_strength == doctest::Approx(pipeline.layers[2].opacity));
}

TEST_CASE("GPU canary glow uses the same three-lobe contract as software") {
    const auto params = GlowPresets::canary_black(42.0f);
    const auto lobes = synthetic_glow_lobes(params);

    CHECK(lobes[0].radius_factor == doctest::Approx(0.10f));
    CHECK(lobes[0].strength == doctest::Approx(0.48f));
    CHECK(lobes[1].radius_factor == doctest::Approx(0.35f));
    CHECK(lobes[1].strength == doctest::Approx(0.30f));
    CHECK(lobes[2].radius_factor == doctest::Approx(1.00f));
    CHECK(lobes[2].strength == doctest::Approx(0.10f));
    CHECK(lobes[2].buffer_scale == doctest::Approx(1.0f));

    CHECK(42.0f * lobes[0].radius_factor == doctest::Approx(4.2f));
    CHECK(42.0f * lobes[1].radius_factor == doctest::Approx(14.7f));
    CHECK(42.0f * lobes[2].radius_factor == doctest::Approx(42.0f));
}

TEST_CASE("PR2-Unit-ShadowBounds: halo matches contact and ambient shadow maps") {
    DropShadowParams p;
    p.offset = {0.0f, 4.0f};
    p.radius = 12.0f;

    const auto halo = effects::drop_shadow_spatial_halo(p);
    // ambient blur=max(5.4+4, 24)=24; blur padding=24+2=26;
    // ambient offset is 4*1.65=6.6 on the positive Y side.
    CHECK(halo.left == doctest::Approx(26.0f));
    CHECK(halo.top == doctest::Approx(26.0f));
    CHECK(halo.right == doctest::Approx(26.0f));
    CHECK(halo.bottom == doctest::Approx(32.6f));
}
