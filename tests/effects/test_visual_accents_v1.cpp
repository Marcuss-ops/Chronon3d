#include <doctest/doctest.h>

#include <chronon3d/graphics/paint_mask_generator.hpp>
#include <chronon3d/graphics/stroke/brush_profile.hpp>
#include <chronon3d/render_plan/effect_presets.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>

#include <cmath>
#include <limits>
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

TEST_CASE("Visual Accents V1: Light leak profile resolver") {
    using namespace light_leak;
    SUBCASE("canonical profile lowers deterministically to existing composition primitives") {
        LightLeakProfile profile;
        profile.shape = LightLeakShape::Sweep;
        profile.blend = LightLeakBlend::SoftLight;
        profile.angle = 45.0f;
        profile.position = 0.25f;
        profile.width = 0.30f;
        profile.chroma_split = 2.0f;
        profile.grain = 0.1f;
        profile.seed = 10;
        const auto first = LightLeakResolver::resolve(profile, Vec2{1920.0f, 1080.0f}, "leak");
        const auto second = LightLeakResolver::resolve(profile, Vec2{1920.0f, 1080.0f}, "leak");
        REQUIRE(first.shape.has_value());
        REQUIRE(second.shape.has_value());
        CHECK(first.id == second.id);
        CHECK(first.type == LayerType::Shape);
        CHECK(first.screen_space);
        CHECK(first.position[0] == doctest::Approx(960.0f));
        CHECK(first.position[1] == doctest::Approx(540.0f));
        CHECK(first.position[2] == doctest::Approx(0.0f));
        CHECK(first.blend_mode == chronon3d::BlendMode::SoftLight);
        REQUIRE(first.shape->gradient.has_value());
        CHECK(first.shape->gradient->type == GradientType::Linear);
        CHECK(first.shape->gradient->start.x == second.shape->gradient->start.x);
        CHECK(first.shape->gradient->start.y == second.shape->gradient->start.y);
        CHECK(first.effects.size() == 3); // softness, RGB split, seeded static grain
        CHECK(first.effects[0].kind == LayerPlan::EffectKindPlan::GaussianBlur);
        CHECK(first.effects[0].radius == doctest::Approx(37.8f));
        CHECK(first.effects[1].kind == LayerPlan::EffectKindPlan::ChromaticAberration);
        CHECK(first.effects[2].kind == LayerPlan::EffectKindPlan::Noise);
        CHECK(first.effects[2].seed == 10);
        CHECK_FALSE(first.effects[2].animated);
    }

    SUBCASE("edge position, corner, radial and band geometry resolve with exact controls") {
        LightLeakProfile profile;
        profile.shape = LightLeakShape::Edge;
        profile.edge = LightLeakEdge::Right;
        profile.position = 0.25f;
        auto edge = LightLeakResolver::resolve(profile, Vec2{100.0f, 100.0f});
        REQUIRE(edge.shape.has_value());
        REQUIRE(edge.shape->gradient.has_value());
        CHECK(edge.shape->gradient->center.x == doctest::Approx(1.0f));
        CHECK(edge.shape->gradient->center.y == doctest::Approx(0.25f));
        profile.edge = LightLeakEdge::Bottom;
        profile.position = 0.75f;
        edge = LightLeakResolver::resolve(profile, Vec2{100.0f, 100.0f});
        REQUIRE(edge.shape->gradient.has_value());
        CHECK(edge.shape->gradient->center.x == doctest::Approx(0.75f));
        CHECK(edge.shape->gradient->center.y == doctest::Approx(1.0f));
        profile.shape = LightLeakShape::Corner;
        profile.corner = LightLeakCorner::BottomLeft;
        auto corner = LightLeakResolver::resolve(profile, Vec2{100.0f, 100.0f});
        REQUIRE(corner.shape.has_value());
        REQUIRE(corner.shape->gradient.has_value());
        CHECK(corner.shape->gradient->center.x == doctest::Approx(0.0f));
        CHECK(corner.shape->gradient->center.y == doctest::Approx(1.0f));
        profile.shape = LightLeakShape::Radial;
        profile.spread = 0.4f;
        profile.position = 0.25f;
        profile.position_y = 0.75f;
        auto radial = LightLeakResolver::resolve(profile, Vec2{100.0f, 100.0f});
        REQUIRE(radial.shape.has_value());
        REQUIRE(radial.shape->gradient.has_value());
        CHECK(radial.shape->gradient->center.x == doctest::Approx(0.25f));
        CHECK(radial.shape->gradient->center.y == doctest::Approx(0.75f));
        CHECK(radial.shape->gradient->radius == doctest::Approx(0.4f * std::sqrt(2.0f)));
        CHECK(radial.shape->gradient->color_stops.size() == 3);
        profile.shape = LightLeakShape::Band;
        profile.angle = 90.0f;
        const auto band = LightLeakResolver::resolve(profile, Vec2{100.0f, 100.0f});
        REQUIRE(band.shape.has_value());
        REQUIRE(band.shape->gradient.has_value());
        CHECK(std::abs(band.shape->gradient->start.x - band.shape->gradient->end.x) > 0.24f);
        CHECK(std::abs(band.shape->gradient->start.y - band.shape->gradient->end.y) < 1.0e-5f);
        CHECK(band.shape->gradient->color_stops.size() == 5);
        CHECK(band.shape->gradient->color_stops[0].color.a == doctest::Approx(0.0f));
        CHECK(band.shape->gradient->color_stops[2].color.a == doctest::Approx(profile.intensity));
        CHECK(band.shape->gradient->color_stops[4].color.a == doctest::Approx(0.0f));
        CHECK(band.shape->gradient->color_stops[2].color.r ==
              doctest::Approx(profile.color_a.r));
        CHECK(band.shape->gradient->color_stops[2].color.a ==
              doctest::Approx(profile.intensity * profile.color_a.a));
    }

    SUBCASE("zero intensity is identity; flicker is explicit and seed-bound") {
        LightLeakProfile profile;
        profile.intensity = 0.0f;
        auto identity = LightLeakResolver::resolve(profile, Vec2{1920.0f, 1080.0f});
        REQUIRE(identity.shape.has_value());
        REQUIRE(identity.shape->gradient.has_value());
        CHECK(identity.shape->gradient->color_stops[0].color.a == doctest::Approx(0.0f));
        CHECK(identity.effects.empty());
        profile.intensity = 1.0f;
        profile.softness = 0.0f;
        profile.flicker = 0.25f;
        profile.seed = 10;
        auto animated = LightLeakResolver::resolve(profile, Vec2{1920.0f, 1080.0f});
        REQUIRE(animated.effects.size() == 1);
        CHECK(animated.effects[0].kind == LayerPlan::EffectKindPlan::Noise);
        CHECK(animated.effects[0].animated);
        REQUIRE(animated.effects.size() == 1);
        CHECK(animated.effects[0].seed == (10U ^ 0x9e3779b9U));
        profile.grain = 0.2f;
        profile.seed = 11;
        auto other_seed = LightLeakResolver::resolve(profile, Vec2{1920.0f, 1080.0f});
        REQUIRE(other_seed.effects.size() == 2);
        CHECK(other_seed.effects[0].seed == 11U);
        CHECK(other_seed.effects[1].seed == (11U ^ 0x9e3779b9U));
        CHECK_FALSE(other_seed.effects[0].animated);
        CHECK(other_seed.effects[1].animated);
        REQUIRE(other_seed.shape.has_value());
        REQUIRE(other_seed.shape->gradient.has_value());
        CHECK(other_seed.shape->gradient->color_stops[0].color.r ==
              doctest::Approx(profile.color_a.r));
        CHECK(other_seed.shape->gradient->color_stops[0].color.a == doctest::Approx(profile.intensity));
        CHECK(other_seed.shape->gradient->color_stops[1].color.a == doctest::Approx(0.0f));
    }

    SUBCASE("profile-generated shape and effects compile through the canonical plan") {
        LightLeakProfile profile;
        profile.shape = LightLeakShape::Radial;
        profile.blend = LightLeakBlend::Add;
        profile.intensity = 0.6f;
        profile.softness = 0.0f;
        profile.chroma_split = 2.0f;
        profile.grain = 0.15f;
        profile.seed = 73;
        auto leak = LightLeakResolver::resolve(profile, Vec2{32.0f, 32.0f}, "profile-leak");
        RenderPlan plan;
        plan.job_id = "light-leak-profile";
        plan.canvas = {.width = 32, .height = 32,
                       .fps = FrameRate{30, 1}, .duration = Frame{2}};
        plan.layers.push_back(std::move(leak));
        chronon3d::assets::AssetResolver resolver;
        const auto prepared = compile_render_plan(plan, resolver);
        REQUIRE(prepared);
        const auto evaluated = chronon3d::evaluate(
            prepared->compiled_composition, {}, Frame{0});
        REQUIRE(evaluated);
        REQUIRE(evaluated->scene.layers().size() == 1);
        const auto& layer = evaluated->scene.layers().front();
        CHECK(layer.screen_space);
        CHECK(layer.blend_mode == chronon3d::BlendMode::Add);
        REQUIRE(layer.nodes.size() == 1);
        CHECK(layer.nodes.front().shape.type() == chronon3d::ShapeType::Rect);
        CHECK(layer.nodes.front().fill.type == chronon3d::FillType::RadialGradient);
        CHECK(layer.nodes.front().fill.gradient.center.x == doctest::Approx(0.5f));
        CHECK(layer.nodes.front().fill.gradient.center.y == doctest::Approx(0.5f));
        REQUIRE(layer.nodes.front().fill.gradient.color_stops.size() == 3);
        CHECK(layer.nodes.front().fill.gradient.color_stops[0].color.a ==
              doctest::Approx(0.6f));
        REQUIRE(layer.effects().size() == 2);
        const auto* split = std::get_if<ChromaticAberrationParams>(
            &layer.effects()[0].params);
        REQUIRE(split);
        CHECK(split->red_offset == doctest::Approx(2.0f));
        const auto* grain = std::get_if<NoiseParams>(&layer.effects()[1].params);
        REQUIRE(grain);
        CHECK(grain->seed == 73U);
        CHECK_FALSE(grain->animated);
    }

    SUBCASE("profile intensity is bounded across SDR RGB and alpha channels") {
        LightLeakProfile profile;
        profile.shape = LightLeakShape::Radial;
        profile.intensity = 2.0f;
        profile.color_a = Color{0.7f, 0.6f, 0.5f, 0.4f};
        auto layer = LightLeakResolver::resolve(profile, Vec2{64.0f, 64.0f});
        REQUIRE(layer.shape);
        REQUIRE(layer.shape->gradient);
        CHECK(layer.shape->gradient->color_stops[0].color.r == doctest::Approx(0.7f));
        CHECK(layer.shape->gradient->color_stops[0].color.a == doctest::Approx(0.8f));
        profile.intensity = light_leak::kMaxIntensity;
        layer = LightLeakResolver::resolve(profile, Vec2{64.0f, 64.0f});
        CHECK(layer.shape->gradient->color_stops[0].color.r == doctest::Approx(0.7f));
        CHECK(layer.shape->gradient->color_stops[0].color.a == doctest::Approx(1.0f));
    }

    SUBCASE("profile validation rejects non-finite and out-of-range controls") {
        LightLeakProfile profile;
        CHECK(profile.valid());
        profile.angle = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(profile.valid());
        profile = {};
        profile.chroma_split = 17.0f;
        CHECK_THROWS_AS(LightLeakResolver::resolve(profile, Vec2{100.0f, 100.0f}), std::invalid_argument);
        profile = {};
        profile.position_y = -0.1f;
        CHECK_FALSE(profile.valid());
        profile = {};
        profile.blend = static_cast<LightLeakBlend>(255);
        CHECK_FALSE(profile.valid());
        profile = {};
        CHECK_THROWS_AS(LightLeakResolver::resolve(profile, Vec2{0.0f, 100.0f}), std::invalid_argument);
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

    SUBCASE("maximum accepted intensity stays within the response bound") {
        LayerPlan layer;
        light_leak::film_burn(layer, light_leak::kMaxIntensity);
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
