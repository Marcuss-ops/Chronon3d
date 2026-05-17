#include <doctest/doctest.h>

#include <chronon3d/effects.hpp>
#include <stdexcept>

using namespace chronon3d::effects;

TEST_CASE("EffectRegistry registers built-in effects with stable ids") {
    EffectRegistry registry;

    const auto ids = registry.available();
    REQUIRE(ids.size() == 7);
    CHECK(ids[0] == "blur.gaussian");
    CHECK(ids[1] == "color.brightness");
    CHECK(ids[2] == "color.contrast");
    CHECK(ids[3] == "color.tint");
    CHECK(ids[4] == "light.bloom");
    CHECK(ids[5] == "light.drop_shadow");
    CHECK(ids[6] == "light.glow");

    const auto& blur = registry.get("blur.gaussian");
    CHECK(blur.category == EffectCategory::Blur);
    CHECK(blur.stage == EffectStage::LayerPostTransform);
    CHECK(blur.builtin);

    const auto& tint = registry.get("color.tint");
    CHECK(tint.category == EffectCategory::Color);
    CHECK(tint.stage == EffectStage::Adjustment);
    CHECK(tint.builtin);
}

TEST_CASE("EffectRegistry rejects duplicate ids") {
    EffectRegistry registry;

    CHECK_THROWS_AS(
        registry.register_effect(EffectDescriptor{
            .id = "color.tint",
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
    auto desc = reg.get("blur.gaussian");
    CHECK(desc.stage == EffectStage::LayerPostTransform);
    CHECK(desc.factory != nullptr); 
}
