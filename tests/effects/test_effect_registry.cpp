#include <doctest/doctest.h>

#include <chronon3d/effects.hpp>
#include <stdexcept>
#include <string>

using namespace chronon3d::effects;
namespace effect_ids = chronon3d::effects::ids;

TEST_CASE("EffectRegistry registers built-in effects with stable ids") {
    EffectRegistry registry;

    const auto available_ids = registry.available();
    REQUIRE(available_ids.size() == 7);
    CHECK(available_ids[0] == std::string(effect_ids::BlurGaussian));
    CHECK(available_ids[1] == std::string(effect_ids::ColorBrightness));
    CHECK(available_ids[2] == std::string(effect_ids::ColorContrast));
    CHECK(available_ids[3] == std::string(effect_ids::ColorTint));
    CHECK(available_ids[4] == std::string(effect_ids::LightBloom));
    CHECK(available_ids[5] == std::string(effect_ids::LightDropShadow));
    CHECK(available_ids[6] == std::string(effect_ids::LightGlow));

    const auto& blur = registry.get(effect_ids::BlurGaussian);
    CHECK(blur.category == EffectCategory::Blur);
    CHECK(blur.stage == EffectStage::LayerPostTransform);
    CHECK(blur.builtin);

    const auto& tint = registry.get(effect_ids::ColorTint);
    CHECK(tint.category == EffectCategory::Color);
    CHECK(tint.stage == EffectStage::Adjustment);
    CHECK(tint.builtin);
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

TEST_CASE("EffectInstance stores descriptor and opaque params") {
    EffectDescriptor descriptor{
        .id = "stylize.demo",
        .display_name = "Demo",
        .category = EffectCategory::Stylize,
        .stage = EffectStage::Composition,
    };

    EffectInstance instance{descriptor, std::string{"payload"}};

    CHECK(instance.enabled);
    CHECK(instance.descriptor.id == "stylize.demo");
    CHECK(instance.has_params());
    CHECK(std::any_cast<std::string>(&instance.params) != nullptr);
    CHECK(*std::any_cast<std::string>(&instance.params) == "payload");
}

TEST_CASE("effect registry is data only") {
    auto& reg = EffectRegistry::instance();
    auto desc = reg.get(effect_ids::BlurGaussian);
    CHECK(desc.stage == EffectStage::LayerPostTransform);
    CHECK(desc.factory != nullptr); 
}
