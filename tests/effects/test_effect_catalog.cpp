#include <doctest/doctest.h>

#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/effects/effect_descriptor.hpp>
#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_stage.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <stdexcept>
#include <string>
using namespace chronon3d;

using namespace chronon3d::effects;
namespace effect_ids = chronon3d::effects::ids;

TEST_CASE("EffectCatalog registers built-in effects with stable ids") {
    EffectCatalog catalog;

    const auto available_ids = catalog.available();
    REQUIRE(available_ids.size() == 57);
    CHECK(catalog.contains(effect_ids::BlurGaussian));
    CHECK(catalog.contains(effect_ids::ColorBrightness));
    CHECK(catalog.contains(effect_ids::ColorContrast));
    CHECK(catalog.contains(effect_ids::ColorTint));
    CHECK(catalog.contains(effect_ids::ColorSaturation));
    CHECK(catalog.contains(effect_ids::ColorHueRotate));
    CHECK(catalog.contains(effect_ids::ColorInvert));
    CHECK(catalog.contains(effect_ids::ColorMatrix));
    CHECK(catalog.contains(effect_ids::ColorPosterize));
    CHECK(catalog.contains(effect_ids::ColorColorMap));
    CHECK(effect_type_for<ColorMapParams>::value == EffectType::ColorMap);
    CHECK((std::is_same_v<effect_params_for<EffectType::ColorMap>::type,
                          ColorMapParams>));
    CHECK(EffectType::ColorMap == static_cast<EffectType>(57));
    CHECK(effect_type_for<PosterizeParams>::value == EffectType::Posterize);
    CHECK((std::is_same_v<effect_params_for<EffectType::Posterize>::type,
                          PosterizeParams>));
    CHECK(catalog.contains(effect_ids::ColorLottieTint));
    CHECK(catalog.contains(effect_ids::ColorLottieTritone));
    CHECK(catalog.contains(effect_ids::ColorVignette));
    CHECK(catalog.contains(effect_ids::DistortFake3DWave));
    CHECK(catalog.contains(effect_ids::LightBloom));
    CHECK(catalog.contains(effect_ids::LightDropShadow));
    CHECK(catalog.contains(effect_ids::LightGlow));
    CHECK(catalog.contains(effect_ids::StylizeChromaticAberration));
    CHECK(catalog.contains(effect_ids::StylizeHalftone));
    CHECK(catalog.contains(effect_ids::DistortLensDistortion));
    CHECK(catalog.contains(effect_ids::LightLuminanceResponse));
    CHECK(catalog.contains(effect_ids::DistortFoldWarp));
    CHECK(catalog.contains(effect_ids::LightCreaseShading));
    CHECK(catalog.contains(effect_ids::StylizeRoughenEdges));

    const auto& blur = catalog.get(effect_ids::BlurGaussian);
    CHECK(blur.category == EffectCategory::Blur);
    CHECK(blur.stage == EffectStage::LayerPostTransform);
    CHECK(blur.builtin);

    const auto& tint = catalog.get(effect_ids::ColorTint);
    CHECK(tint.category == EffectCategory::Color);
    CHECK(tint.stage == EffectStage::Adjustment);
    CHECK(tint.builtin);

    const auto& wave = catalog.get(effect_ids::DistortFake3DWave);
    CHECK(wave.category == EffectCategory::Distort);
    CHECK(wave.stage == EffectStage::LayerPostTransform);
    CHECK(wave.temporal);

    // AE-5: new adjustment effects
    const auto& sat = catalog.get(effect_ids::ColorSaturation);
    CHECK(sat.category == EffectCategory::Color);
    CHECK(sat.stage == EffectStage::Adjustment);
    CHECK(sat.builtin);

    const auto& hue = catalog.get(effect_ids::ColorHueRotate);
    CHECK(hue.category == EffectCategory::Color);
    CHECK(hue.stage == EffectStage::Adjustment);
    CHECK(hue.builtin);

    const auto& inv = catalog.get(effect_ids::ColorInvert);
    CHECK(inv.category == EffectCategory::Color);
    CHECK(inv.stage == EffectStage::Adjustment);
    CHECK(inv.builtin);

    const auto& vig = catalog.get(effect_ids::ColorVignette);
    CHECK(vig.category == EffectCategory::Color);
    CHECK(vig.stage == EffectStage::Adjustment);
    CHECK(vig.builtin);
}

TEST_CASE("Posterize catalog parameters validate and expose bounded animation") {
    CHECK(EffectType::Posterize == static_cast<EffectType>(56));
    PosterizeParams params;
    params.validate();
    CHECK_THROWS_AS((PosterizeParams{.levels = 1}).validate(), std::invalid_argument);
    CHECK_THROWS_AS((PosterizeParams{.levels = 257}).validate(), std::invalid_argument);

    CHECK(find_animatable_effect_param(effect_ids::ColorPosterize, "levels") != nullptr);
    CHECK(set_animatable_effect_param(params, effect_ids::ColorPosterize, "levels", 12.0f));
    CHECK(params.levels == 12);
    CHECK(get_animatable_effect_param(params, effect_ids::ColorPosterize, "levels") == 12.0f);
}

TEST_CASE("ColorMatrix catalog parameters preserve row-major mapping and bounded animation") {
    const auto defaults = make_default_animatable_effect_params(effect_ids::ColorMatrix);
    REQUIRE(defaults.has_value());
    REQUIRE(std::holds_alternative<ColorMatrixParams>(*defaults));
    auto matrix = std::get<ColorMatrixParams>(*defaults);
    for (std::size_t i = 0; i < kColorMatrixParamNames.size(); ++i) {
        const auto name = kColorMatrixParamNames[i];
        const auto* spec = find_animatable_effect_param(effect_ids::ColorMatrix, name);
        REQUIRE(spec != nullptr);
        CHECK(spec->minimum == -16.0f);
        CHECK(spec->maximum == 16.0f);
        for (const float value : {-16.0f, 0.25f, 16.0f}) {
            REQUIRE(set_animatable_effect_param(matrix, effect_ids::ColorMatrix, name, value));
            CHECK(matrix.values[i] == value);
            CHECK(get_animatable_effect_param(matrix, effect_ids::ColorMatrix, name) == value);
        }
        for (const float invalid : {-16.01f, 16.01f, INFINITY, NAN}) {
            CHECK_FALSE(set_animatable_effect_param(matrix, effect_ids::ColorMatrix, name, invalid));
            CHECK(matrix.values[i] == 16.0f);
        }
    }
    CHECK_FALSE(set_animatable_effect_param(matrix, effect_ids::ColorMatrix, "m44", 1.0f));
    CHECK_FALSE(get_animatable_effect_param(matrix, effect_ids::ColorMatrix, "m44").has_value());
}

TEST_CASE("EffectCatalog rejects duplicate ids") {
    EffectCatalog catalog;

    CHECK_THROWS_AS(
        catalog.register_effect(EffectDescriptor{
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

TEST_CASE("EffectCatalog: factory available after freeze") {
    EffectCatalog catalog;
    catalog.freeze();
    auto desc = catalog.get(effect_ids::BlurGaussian);
    CHECK(desc.stage == EffectStage::LayerPostTransform);
    CHECK(desc.factory != nullptr); 
    CHECK(catalog.find(effect_ids::BlurGaussian) != nullptr);
    CHECK(catalog.find("nonexistent_effect") == nullptr);
}
