// Registry census + decode fail-fast contract for layer animation properties.
//
// The registry (animation_property_registry.hpp) is the single inventory of
// `layers[].animation.tracks[].property` names. These tests pin three facts:
//   1. the table has no duplicate names and matches its declared arity;
//   2. every property the layer decoder already accepts still decodes
//      (the gate must not reject the existing corpus);
//   3. an unregistered property name fails decode with a nearest-name
//      suggestion, instead of decoding silently and being dropped later.

#include <doctest/doctest.h>

#include <chronon3d/render_plan/animation_property_registry.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include "test_render_plan_shape_helpers.hpp"

#include <set>
#include <string>
#include <nlohmann/json.hpp>

TEST_CASE("animation property registry is a duplicate-free bounded inventory") {
    std::set<std::string_view> names;
    for (const auto& row : chronon3d::render_plan::kAnimationPropertyTable) {
        INFO("duplicate registry row: ", row.name);
        REQUIRE(names.insert(row.name).second);
        REQUIRE(!row.name.empty());
    }
    // Declared census: 30 named properties + 15 explicit shadow rows
    // (shadow_<0..2> x <opacity|blur|offset_x|offset_y|color>). Growing the
    // table is a contract change: bump this bound deliberately, never by
    // accident.
    REQUIRE(chronon3d::render_plan::kAnimationPropertyTable.size() == 45);
}

TEST_CASE("registered scalar and articulated layer tracks still decode") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "tracked"}, {"type", "color"}, {"color", {0.1, 0.2, 0.3, 1.0}}}
    }));
    source["layers"][0]["animation"] = {
        {"tracks", nlohmann::json::array({
            {{"property", "position_y"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", 10.0}},
                {{"frame", 6}, {"value", 40.0}}})}},
            {{"property", "opacity"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", 0.0}},
                {{"frame", 6}, {"value", 1.0}}})}},
            {{"property", "rotation_z"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", 0.0}},
                {{"frame", 6}, {"value", 90.0}}})}}
        })}
    };
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) MESSAGE("decode failed: ", decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].animation);
    CHECK(decoded->layers[0].animation->tracks.size() == 3);
}

TEST_CASE("unregistered layer track property fails decode with a suggestion") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "tracked"}, {"type", "color"}, {"color", {0.1, 0.2, 0.3, 1.0}}}
    }));
    source["layers"][0]["animation"] = {
        {"tracks", nlohmann::json::array({
            {{"property", "opacitiy"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", 1.0}}})}}
        })}
    };
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE_FALSE(decoded);
    // The typed rejection names the offending property and proposes the
    // nearest registered name — no silent acceptance, no bare enum listing.
    CHECK(decoded.error().message.find("unregistered property") !=
          std::string::npos);
    CHECK(decoded.error().message.find("opacitiy") != std::string::npos);
    CHECK(decoded.error().message.find("opacity") != std::string::npos);
}

TEST_CASE("nearest-name diagnostics stay silent beyond the edit-distance budget") {
    using chronon3d::render_plan::nearest_animation_property_name;
    CHECK(nearest_animation_property_name("opacitiy") == "opacity");
    CHECK(nearest_animation_property_name("position-z") == "position_z");
    // No registered name is within distance 2 of this string.
    CHECK(nearest_animation_property_name("completely_unrelated").empty());
}

TEST_CASE("decode gate accepts the full registered family on a shape layer") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({
        render_plan_test::shape_layer({
            {"type", "rect"},
            {"fill", {1.0, 0.0, 0.0, 1.0}},
            {"stroke", {{"color", "#FF0000"}, {"width", 2.0}}}
        })
    }));
    source["layers"][0]["style"] = {
        {"shadows", nlohmann::json::array({
            {{"color", "#000000"}, {"opacity", 0.5}, {"blur", 4.0},
             {"offset", {2.0, 2.0}}},
            {{"color", "#101010"}, {"opacity", 0.4}, {"blur", 3.0},
             {"offset", {1.0, 1.0}}}
        })}
    };
    source["layers"][0]["animation"] = {
        {"tracks", nlohmann::json::array({
            {{"property", "shadow_0_opacity"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", 0.0}}, {{"frame", 4}, {"value", 0.5}}})}},
            {{"property", "shadow_1_blur"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", 1.0}}, {{"frame", 4}, {"value", 3.0}}})}},
            {{"property", "stroke_width"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", 1.0}}, {{"frame", 4}, {"value", 5.0}}})}},
            {{"property", "fill_color"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", {1.0, 0.0, 0.0, 1.0}}},
                {{"frame", 4}, {"value", {0.0, 1.0, 0.0, 1.0}}}})}}
        })}
    };
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) MESSAGE("decode failed: ", decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].animation);
    CHECK(decoded->layers[0].animation->tracks.size() == 4);
}
