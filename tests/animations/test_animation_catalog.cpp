#include <chronon3d/api/animations.hpp>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <set>
#include <string>

using namespace chronon3d::api;

TEST_CASE("Builtin animation catalog exposes stable reusable clips") {
    const auto& catalog = builtin_animation_catalog();

    REQUIRE(catalog.clips.size() >= 5);

    std::set<std::string_view> ids;
    for (const auto& clip : catalog.clips) {
        CHECK_FALSE(clip.id.empty());
        CHECK_FALSE(clip.name.empty());
        CHECK_FALSE(clip.family.empty());
        CHECK(clip.duration_seconds > 0.0f);
        CHECK(clip.fps > 0);
        CHECK(ids.insert(clip.id).second);
    }

    const auto* studio_grid = find_builtin_animation("studio_grid_loop");
    REQUIRE(studio_grid != nullptr);
    CHECK(studio_grid->name == "Studio Grid Loop");
    CHECK(studio_grid->loopable);

    const std::string json = builtin_animation_catalog_json();
    const auto parsed = nlohmann::json::parse(json);
    REQUIRE(parsed.is_array());
    REQUIRE(parsed.size() == catalog.clips.size());
}

TEST_CASE("C ABI returns the same manifest JSON") {
    const std::string json = chronon3d_animation_catalog_json();
    const auto parsed = nlohmann::json::parse(json);
    REQUIRE(parsed.is_array());
    CHECK(parsed.size() >= 5);
}
