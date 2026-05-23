#include <chronon3d/api/backgrounds.hpp>
#include <chronon3d/core/composition_registry.hpp>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

using namespace chronon3d::api;

TEST_CASE("Builtin background catalog exposes easy reusable presets") {
    const auto& catalog = builtin_background_catalog();

    REQUIRE(catalog.presets.size() >= 4);

    const auto* aurora = find_builtin_background("easy_aurora");
    REQUIRE(aurora != nullptr);
    CHECK(aurora->name == "Easy Aurora");
    CHECK(aurora->loopable);

    const std::string json = builtin_background_catalog_json();
    const auto parsed = nlohmann::json::parse(json);
    REQUIRE(parsed.is_array());
    CHECK(parsed.size() == catalog.presets.size());
}

TEST_CASE("Builtin background compositions are registered") {
    chronon3d::CompositionRegistry registry;
    CHECK(registry.create("EasyAuroraBackground").name() == "EasyAuroraBackground");
    CHECK(registry.create("EasyGridBackground").name() == "EasyGridBackground");
    CHECK(registry.create("EasyRibbonBackground").name() == "EasyRibbonBackground");
    CHECK(registry.create("EasyOrbitBackground").name() == "EasyOrbitBackground");
}
