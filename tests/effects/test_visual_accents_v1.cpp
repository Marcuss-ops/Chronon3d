#include <doctest/doctest.h>

#include <chronon3d/graphics/paint_mask_generator.hpp>
#include <chronon3d/graphics/stroke/brush_profile.hpp>
#include <chronon3d/render_plan/effect_presets.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <cmath>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::graphics;
using namespace chronon3d::render_plan;
using namespace chronon3d::render_plan::effect_presets;

namespace {

/// Coarse-grid Monte Carlo of the paint coverage: the fraction of a 16x16
/// normalized grid the generator reveals. Deterministic, allocation-free.
float paint_coverage(const PaintMaskGenerator& generator) {
    int revealed = 0;
    const int grid = 16;
    for (int y = 0; y < grid; ++y) {
        for (int x = 0; x < grid; ++x) {
            const Vec2 point{static_cast<float>(x + 0.5) / grid,
                             static_cast<float>(y + 0.5) / grid};
            if (generator.sample(point) > 0.5f) ++revealed;
        }
    }
    return static_cast<float>(revealed) / static_cast<float>(grid * grid);
}

} // namespace

TEST_CASE("Visual Accents V1: PaintMaskGenerator authority") {
    SUBCASE("progress anchors: 0 reveals nothing, 1 reveals the full frame") {
        auto generator = PaintMaskGenerator::blob(42);
        generator.progress = 0.0f;
        CHECK(paint_coverage(generator) == 0.0f);
        generator.progress = 1.0f;
        CHECK(paint_coverage(generator) == 1.0f);
    }

    SUBCASE("same seed produces the identical mask, different seed does not") {
        auto a1 = PaintMaskGenerator::dry_brush_reveal(4242);
        auto a2 = PaintMaskGenerator::dry_brush_reveal(4242);
        auto b = PaintMaskGenerator::dry_brush_reveal(909);
        a1.progress = a2.progress = b.progress = 0.6f;
        bool same = true, different = false;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const Vec2 point{static_cast<float>(x + 0.25) / 8,
                                 static_cast<float>(y + 0.25) / 8};
                const float v1 = a1.sample(point);
                if (v1 != a2.sample(point)) same = false;
                if (std::abs(v1 - b.sample(point)) > 1.0e-3f) different = true;
            }
        }
        CHECK(same);
        CHECK(different);
    }

    SUBCASE("mask values stay in [0, 1] and are finite everywhere") {
        auto generator = PaintMaskGenerator::multi_blob(77, 5);
        generator.progress = 0.5f;
        for (int y = -4; y <= 12; ++y) {
            for (int x = -4; x <= 12; ++x) {
                const Vec2 point{static_cast<float>(x) / 8, static_cast<float>(y) / 8};
                const float value = generator.sample(point);
                CHECK(std::isfinite(value));
                CHECK(value >= 0.0f);
                CHECK(value <= 1.0f);
            }
        }
    }

    SUBCASE("ink-spread monotonicity: coverage grows with progress") {
        auto generator = PaintMaskGenerator::blob(2026, Vec2{0.5f, 0.5f}, 0.6f);
        float previous = -1.0f;
        for (int step = 1; step <= 10; ++step) {
            generator.progress = static_cast<float>(step) / 10.0f;
            const float coverage = paint_coverage(generator);
            CHECK(coverage >= previous);
            previous = coverage;
        }
        CHECK(previous == 1.0f);
    }

    SUBCASE("multi-blob merge reaches full coverage like the single blob") {
        auto generator = PaintMaskGenerator::multi_blob(555, 4);
        generator.progress = 1.0f;
        CHECK(paint_coverage(generator) == 1.0f);
    }

    SUBCASE("wipe cover is deterministic and edge-soft") {
        auto generator = PaintMaskGenerator::wipe(31, 0.0f);
        generator.progress = 0.5f;
        const float left = generator.sample(Vec2{0.2f, 0.5f});
        const float right = generator.sample(Vec2{0.8f, 0.5f});
        CHECK(std::isfinite(left));
        CHECK(std::isfinite(right));
        // Deterministic double sample.
        CHECK(left == generator.sample(Vec2{0.2f, 0.5f}));
    }
}

TEST_CASE("Visual Accents V1: Light leak recipes") {
    SUBCASE("intensity 0 is an exact no-op") {
        LayerPlan neutral;
        LayerPlan quiet;
        light_leak::edge_warm(quiet, 0.0f);
        light_leak::corner_bloom(quiet, 0.0f);
        light_leak::horizontal_sweep(quiet, 0.0f);
        light_leak::diagonal_sweep(quiet, 0.0f);
        light_leak::dual_edge(quiet, 0.0f);
        light_leak::prismatic(quiet, 0.0f);
        light_leak::pulse(quiet, 0.0f);
        light_leak::film_burn(quiet, 0.0f);
        light_leak::soft_flare(quiet, 0.0f);
        light_leak::focus_glow(quiet, 0.0f);
        light_leak::bokeh_pass(quiet, 0.0f);
        CHECK(quiet.effects.size() == neutral.effects.size());
    }

    SUBCASE("every recipe is deterministic in its plan output") {
        LayerPlan first, second;
        light_leak::edge_warm(first, 1.2f);
        light_leak::film_burn(first, 2.0f);
        light_leak::prismatic(first, 1.0f);
        light_leak::edge_warm(second, 1.2f);
        light_leak::film_burn(second, 2.0f);
        light_leak::prismatic(second, 1.0f);
        REQUIRE(first.effects.size() == second.effects.size());
        for (std::size_t i = 0; i < first.effects.size(); ++i) {
            CHECK(first.effects[i].kind == second.effects[i].kind);
            CHECK(first.effects[i].response_intensity == second.effects[i].response_intensity);
            CHECK(first.effects[i].response_threshold == second.effects[i].response_threshold);
            CHECK(first.effects[i].glow_intensity == second.effects[i].glow_intensity);
        }
    }

    SUBCASE("energy stays bounded: recipes cap response intensity at 4") {
        LayerPlan layer;
        light_leak::film_burn(layer, 99.0f);
        for (const auto& effect : layer.effects) {
            if (effect.kind == LayerPlan::EffectKindPlan::LuminanceResponse) {
                CHECK(effect.response_intensity <= light_leak::kMaxIntensity);
            }
        }
    }

    SUBCASE("fail-closed on out-of-range parameters") {
        LayerPlan layer;
        CHECK_THROWS_AS(light_leak::edge_warm(layer, -0.1f), std::invalid_argument);
        CHECK_THROWS_AS(light_leak::edge_warm(layer, 4.1f), std::invalid_argument);
        CHECK_THROWS_AS(light_leak::corner_bloom(layer, 1.0f, 7), std::invalid_argument);
        CHECK_THROWS_AS(light_leak::prismatic(layer, 1.0f, 32.0f), std::invalid_argument);
        CHECK_THROWS_AS(light_leak::film_burn(layer, 1.0f, 1.5f), std::invalid_argument);
        CHECK_THROWS_AS(light_leak::bokeh_pass(layer, 1.0f, 2.0f), std::invalid_argument);
        CHECK_THROWS_AS(light_leak::soft_flare(layer, 1.0f, light_leak::kWarmTint, 512.0f), std::invalid_argument);
    }

    SUBCASE("prismatic dispersion stays small and symmetric") {
        LayerPlan layer;
        light_leak::prismatic(layer, 1.0f, 2.0f);
        bool seen_split = false;
        for (const auto& effect : layer.effects) {
            if (effect.kind == LayerPlan::EffectKindPlan::ChromaticAberration) {
                seen_split = true;
                CHECK(effect.chromatic_red_offset == 2.0f);
                CHECK(effect.chromatic_blue_offset == -2.0f);
            }
        }
        CHECK(seen_split);
    }

    SUBCASE("film burn is additive and thresholded for transition mattes") {
        LayerPlan layer;
        light_leak::film_burn(layer, 2.5f, 0.30f);
        REQUIRE(layer.effects.size() == 2);
        CHECK(layer.effects[0].kind == LayerPlan::EffectKindPlan::LuminanceResponse);
        CHECK(layer.effects[0].response_blend == 1); // Add
        CHECK(layer.effects[0].response_threshold == doctest::Approx(0.30f));
        CHECK(layer.effects[1].kind == LayerPlan::EffectKindPlan::Bloom);
    }

    SUBCASE("dual edge composes two opposed responses") {
        LayerPlan layer;
        light_leak::dual_edge(layer, 1.0f);
        REQUIRE(layer.effects.size() == 2);
        CHECK(layer.effects[0].kind == LayerPlan::EffectKindPlan::LuminanceResponse);
        CHECK(layer.effects[1].kind == LayerPlan::EffectKindPlan::LuminanceResponse);
        const bool warm_first = layer.effects[0].response_tint[0] > layer.effects[0].response_tint[2];
        const bool cool_second = layer.effects[1].response_tint[2] > layer.effects[1].response_tint[0];
        CHECK(warm_first);
        CHECK(cool_second);
    }

    SUBCASE("corner bloom anchors rays at the requested corner") {
        LayerPlan layer;
        light_leak::corner_bloom(layer, 1.0f, 3);
        REQUIRE(layer.effects.size() == 2);
        CHECK(layer.effects[0].kind == LayerPlan::EffectKindPlan::LightRays);
        CHECK(layer.effects[0].light_rays_origin[0] == doctest::Approx(1.0f));
        CHECK(layer.effects[0].light_rays_origin[1] == doctest::Approx(1.0f));
        CHECK(layer.effects[1].kind == LayerPlan::EffectKindPlan::Bloom);
    }
}
