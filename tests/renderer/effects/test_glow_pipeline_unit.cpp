// ==============================================================================
// tests/renderer/effects/test_glow_pipeline_unit.cpp
//
// PR2 — GlowPipeline unit tests (3 tests, math/API correctness).
//
// GlowPipeline is the unified struct that captures every parameter any of
// the four glow paths (apply_glow_effect, apply_bloom_effect, text_glow,
// draw_glow) need.  These unit tests verify the conversions and pure-math
// helpers:
//   1. GlowPipeline::from(GlowParams) preserves all important fields.
//   2. GlowPipeline::from(BloomParams) sets Mode::Bloom.
//   3. glow_effect_extent() returns max(implied radius + 4 px).
// ==============================================================================

#include <doctest/doctest.h>

#include <chronon3d/effects/glow_pipeline.hpp>

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
    // Expected: max_layer_r = 20; radius = 20 * 1.0 = 20; extent = 20 + 4 = 24.
    const float e = glow_effect_extent(p);
    CHECK(e == doctest::Approx(24.0f));

    // Synthetic layers override radius: max layer wins.
    GlowLayer layer;
    layer.radius = 50.0f;
    p.layers.push_back(layer);
    const float e2 = glow_effect_extent(p);
    CHECK(e2 == doctest::Approx(54.0f));
}
