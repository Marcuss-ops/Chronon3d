// ---------------------------------------------------------------------------
// test_effect_catalog.cpp — Effect catalog tests (sezione 4 della specifica)
// ---------------------------------------------------------------------------
//
// Copertura:
//   - EffectType static_assert per tutti i valori 0-12 esistenti, +13/14
//   - Mapping bidirezionale: Type → Params → Type, Type → ID → Type
//   - Unicità ID e valori numerici

#include <doctest/doctest.h>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/effects/effect_type.hpp>
#include "tests/effects/test_helpers.hpp"
#include <set>
#include <string>

using namespace chronon3d::effects;

// =============================================================================
// 1. EffectType numeric values (sezione 4.1)
// =============================================================================

TEST_CASE("EffectCatalog: EffectType numeric values are stable") {
    static_assert(static_cast<uint8_t>(EffectType::Unknown)        == 0);
    static_assert(static_cast<uint8_t>(EffectType::Blur)           == 1);
    static_assert(static_cast<uint8_t>(EffectType::Tint)           == 2);
    static_assert(static_cast<uint8_t>(EffectType::Brightness)     == 3);
    static_assert(static_cast<uint8_t>(EffectType::Contrast)       == 4);
    static_assert(static_cast<uint8_t>(EffectType::DropShadow)     == 5);
    static_assert(static_cast<uint8_t>(EffectType::Glow)           == 6);
    static_assert(static_cast<uint8_t>(EffectType::Bloom)          == 7);
    static_assert(static_cast<uint8_t>(EffectType::Fake3DWave)     == 8);
    static_assert(static_cast<uint8_t>(EffectType::Saturation)     == 9);
    static_assert(static_cast<uint8_t>(EffectType::HueRotate)      == 10);
    static_assert(static_cast<uint8_t>(EffectType::Invert)         == 11);
    static_assert(static_cast<uint8_t>(EffectType::Vignette)       == 12);
    static_assert(static_cast<uint8_t>(EffectType::Exposure)       == 13);
    static_assert(static_cast<uint8_t>(EffectType::Levels)         == 14);
    static_assert(static_cast<uint8_t>(EffectType::Curves)         == 15);
    static_assert(static_cast<uint8_t>(EffectType::Fill)           == 16);
    static_assert(static_cast<uint8_t>(EffectType::Noise)          == 17);
    static_assert(static_cast<uint8_t>(EffectType::Offset)         == 18);
    static_assert(static_cast<uint8_t>(EffectType::DirectionalBlur) == 19);
    static_assert(static_cast<uint8_t>(EffectType::RadialBlur)     == 20);
    static_assert(static_cast<uint8_t>(EffectType::Stroke)         == 21);

    // Verify that the enum values we use in code match the expected numeric range
    CHECK(static_cast<uint8_t>(EffectType::Unknown) == 0);
    CHECK(static_cast<uint8_t>(EffectType::Stroke) == 21);
}

// =============================================================================
// 2. effect_type_for<T> mapping (sezione 4.2)
// =============================================================================

TEST_CASE("EffectCatalog: effect_type_for maps param type to EffectType") {
    CHECK(effect_type_for<BlurParams>::value          == EffectType::Blur);
    CHECK(effect_type_for<TintParams>::value          == EffectType::Tint);
    CHECK(effect_type_for<BrightnessParams>::value    == EffectType::Brightness);
    CHECK(effect_type_for<ContrastParams>::value      == EffectType::Contrast);
    CHECK(effect_type_for<DropShadowParams>::value    == EffectType::DropShadow);
    CHECK(effect_type_for<GlowParams>::value          == EffectType::Glow);
    CHECK(effect_type_for<BloomParams>::value         == EffectType::Bloom);
    CHECK(effect_type_for<Fake3DWaveParams>::value    == EffectType::Fake3DWave);
    CHECK(effect_type_for<SaturationParams>::value    == EffectType::Saturation);
    CHECK(effect_type_for<HueRotateParams>::value     == EffectType::HueRotate);
    CHECK(effect_type_for<InvertParams>::value        == EffectType::Invert);
    CHECK(effect_type_for<VignetteParams>::value      == EffectType::Vignette);
    CHECK(effect_type_for<ExposureParams>::value      == EffectType::Exposure);
    CHECK(effect_type_for<LevelsParams>::value        == EffectType::Levels);
    CHECK(effect_type_for<FillParams>::value          == EffectType::Fill);
    CHECK(effect_type_for<NoiseParams>::value         == EffectType::Noise);
    CHECK(effect_type_for<OffsetParams>::value        == EffectType::Offset);
}

// =============================================================================
// 3. effect_params_for<E> reverse mapping (sezione 4.2)
// =============================================================================

TEST_CASE("EffectCatalog: effect_params_for maps EffectType back to param type") {
    CHECK((std::is_same_v<effect_params_for<EffectType::Blur>::type,
          BlurParams>));
    CHECK((std::is_same_v<effect_params_for<EffectType::Tint>::type,
          TintParams>));
    CHECK((std::is_same_v<effect_params_for<EffectType::Exposure>::type,
          ExposureParams>));
    CHECK((std::is_same_v<effect_params_for<EffectType::Levels>::type,
          LevelsParams>));
    CHECK((std::is_same_v<effect_params_for<EffectType::Fill>::type,
          FillParams>));
    CHECK((std::is_same_v<effect_params_for<EffectType::Noise>::type,
          NoiseParams>));
    CHECK((std::is_same_v<effect_params_for<EffectType::Offset>::type,
          OffsetParams>));
}

// =============================================================================
// 4. Bidirectional roundtrip: Type → Params → Type (sezione 4.2)
// =============================================================================

TEST_CASE("EffectCatalog: roundtrip Type->Params->Type is identity") {
    auto test_roundtrip = []<EffectType E>() {
        using ParamsType = typename effect_params_for<E>::type;
        constexpr EffectType back = effect_type_for<ParamsType>::value;
        CHECK(back == E);
    };

    test_roundtrip.operator()<EffectType::Blur>();
    test_roundtrip.operator()<EffectType::Tint>();
    test_roundtrip.operator()<EffectType::Brightness>();
    test_roundtrip.operator()<EffectType::Contrast>();
    test_roundtrip.operator()<EffectType::DropShadow>();
    test_roundtrip.operator()<EffectType::Glow>();
    test_roundtrip.operator()<EffectType::Bloom>();
    test_roundtrip.operator()<EffectType::Fake3DWave>();
    test_roundtrip.operator()<EffectType::Saturation>();
    test_roundtrip.operator()<EffectType::HueRotate>();
    test_roundtrip.operator()<EffectType::Invert>();
    test_roundtrip.operator()<EffectType::Vignette>();
    test_roundtrip.operator()<EffectType::Exposure>();
    test_roundtrip.operator()<EffectType::Levels>();
    test_roundtrip.operator()<EffectType::Fill>();
    test_roundtrip.operator()<EffectType::Noise>();
    test_roundtrip.operator()<EffectType::Offset>();
}

// =============================================================================
// 5. Unicità ID e valori numerici nel catalogo (sezione 4.3)
// =============================================================================

TEST_CASE("EffectCatalog: all IDs and numeric values are unique") {
    std::set<std::string_view> ids;
    std::set<uint8_t> numbers;

    for (const auto& entry : builtin_effect_catalog()) {
        CHECK_MESSAGE(ids.insert(entry.id).second,
                      std::string("Duplicate ID: ").append(entry.id).c_str());
        CHECK_MESSAGE(numbers.insert(static_cast<uint8_t>(entry.type)).second,
                      (std::string("Duplicate numeric value: ")
                       + std::to_string(static_cast<int>(entry.type))).c_str());
    }
}

// =============================================================================
// 6. detect_effect_type via std::visit (sezione 4, indiretta)
// =============================================================================

TEST_CASE("EffectCatalog: detect_effect_type works via visitor") {
    CHECK(detect_effect_type(BlurParams{5.0f})       == EffectType::Blur);
    CHECK(detect_effect_type(TintParams{})            == EffectType::Tint);
    CHECK(detect_effect_type(ExposureParams{1.0f})    == EffectType::Exposure);
    CHECK(detect_effect_type(LevelsParams{})          == EffectType::Levels);
    CHECK(detect_effect_type(FillParams{})            == EffectType::Fill);
    CHECK(detect_effect_type(NoiseParams{})           == EffectType::Noise);
    CHECK(detect_effect_type(OffsetParams{})          == EffectType::Offset);
}

// =============================================================================
// 7. Catalog entries have correct traits (cross-check parziale)
// =============================================================================

TEST_CASE("EffectCatalog: Exposure/Levels have correct traits") {
    for (const auto& entry : builtin_effect_catalog()) {
        if (entry.type == EffectType::Exposure) {
            CHECK(entry.domain == EffectDomain::Color);
            CHECK(entry.alpha_policy == AlphaPolicy::Preserve);
            CHECK(entry.dirty_policy == DirtyPolicy::Preserve);
            CHECK(entry.bounds_policy == BoundsPolicy::Preserve);
            CHECK(entry.fusible_color == true);
        }
        if (entry.type == EffectType::Levels) {
            CHECK(entry.domain == EffectDomain::Color);
            CHECK(entry.alpha_policy == AlphaPolicy::Preserve);
            CHECK(entry.fusible_color == true);
        }
        if (entry.type == EffectType::Fill) {
            CHECK(entry.domain == EffectDomain::Color);
            CHECK(entry.alpha_policy == AlphaPolicy::Preserve);
            CHECK(entry.fusible_color == false);
        }
        if (entry.type == EffectType::Noise) {
            CHECK(entry.alpha_policy == AlphaPolicy::Preserve);
        }
        if (entry.type == EffectType::Offset) {
            CHECK(entry.domain == EffectDomain::Spatial);
            CHECK(entry.alpha_policy == AlphaPolicy::Preserve);
            CHECK(entry.dirty_policy == DirtyPolicy::Translate);
            CHECK(entry.bounds_policy == BoundsPolicy::Translate);
        }
    }
}
