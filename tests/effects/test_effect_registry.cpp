#include <doctest/doctest.h>

#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/effects/effect_descriptor.hpp>
#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_stage.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <stdexcept>
#include <string>

using namespace chronon3d;
using namespace chronon3d::effects;
namespace effect_ids = chronon3d::effects::ids;

TEST_CASE("EffectRegistry registers built-in effects with stable ids") {
    EffectRegistry registry;

    const auto available_ids = registry.available();
    REQUIRE(available_ids.size() == 21);
    CHECK(registry.contains(effect_ids::BlurGaussian));
    CHECK(registry.contains(effect_ids::ColorBrightness));
    CHECK(registry.contains(effect_ids::ColorContrast));
    CHECK(registry.contains(effect_ids::ColorTint));
    CHECK(registry.contains(effect_ids::ColorSaturation));
    CHECK(registry.contains(effect_ids::ColorHueRotate));
    CHECK(registry.contains(effect_ids::ColorInvert));
    CHECK(registry.contains(effect_ids::ColorVignette));
    CHECK(registry.contains(effect_ids::DistortFake3DWave));
    CHECK(registry.contains(effect_ids::LightBloom));
    CHECK(registry.contains(effect_ids::LightDropShadow));
    CHECK(registry.contains(effect_ids::LightGlow));

    const auto& blur = registry.get(effect_ids::BlurGaussian);
    CHECK(blur.category == EffectCategory::Blur);
    CHECK(blur.stage == EffectStage::LayerPostTransform);
    CHECK(blur.builtin);

    const auto& tint = registry.get(effect_ids::ColorTint);
    CHECK(tint.category == EffectCategory::Color);
    CHECK(tint.stage == EffectStage::Adjustment);
    CHECK(tint.builtin);

    const auto& wave = registry.get(effect_ids::DistortFake3DWave);
    CHECK(wave.category == EffectCategory::Distort);
    CHECK(wave.stage == EffectStage::LayerPostTransform);
    CHECK(wave.temporal);

    // AE-5: new adjustment effects
    const auto& sat = registry.get(effect_ids::ColorSaturation);
    CHECK(sat.category == EffectCategory::Color);
    CHECK(sat.stage == EffectStage::Adjustment);
    CHECK(sat.builtin);

    const auto& hue = registry.get(effect_ids::ColorHueRotate);
    CHECK(hue.category == EffectCategory::Color);
    CHECK(hue.stage == EffectStage::Adjustment);
    CHECK(hue.builtin);

    const auto& inv = registry.get(effect_ids::ColorInvert);
    CHECK(inv.category == EffectCategory::Color);
    CHECK(inv.stage == EffectStage::Adjustment);
    CHECK(inv.builtin);

    const auto& vig = registry.get(effect_ids::ColorVignette);
    CHECK(vig.category == EffectCategory::Color);
    CHECK(vig.stage == EffectStage::Adjustment);
    CHECK(vig.builtin);
}

TEST_CASE("EffectRegistry rejects duplicate ids") {
    EffectRegistry registry;

    CHECK_THROWS_AS(
        registry.register_effect(EffectDescriptor{
            .id = std::string{effect_ids::ColorTint},
            .display_name = "Tint 2",
            .category = EffectCategory::Color,
            .stage = EffectStage::Adjustment,
        }),
        std::runtime_error
    );
}

TEST_CASE("EffectInstance stores descriptor and params") {
    EffectDescriptor descriptor{
        .id = "stylize.demo",
        .display_name = "Demo",
        .category = EffectCategory::Stylize,
        .stage = EffectStage::Composition,
    };

    EffectInstance instance{descriptor, BlurParams{5.0f}};

    CHECK(instance.enabled);
    CHECK(instance.descriptor.id == "stylize.demo");
    CHECK(instance.has_params());
    // params is now a variant — extraction via std::get_if is O(1)
    // with no type_info comparison.
    CHECK(std::get_if<BlurParams>(&instance.params) != nullptr);
    CHECK(std::get<BlurParams>(instance.params).radius == doctest::Approx(5.0f));
}

TEST_CASE("effect registry is data only") {
    auto& reg = EffectRegistry::instance();
    auto desc = reg.get(effect_ids::BlurGaussian);
    CHECK(desc.stage == EffectStage::LayerPostTransform);
    CHECK(desc.factory != nullptr); 
}
