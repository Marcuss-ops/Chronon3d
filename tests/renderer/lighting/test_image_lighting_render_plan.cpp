#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include "src/render_plan/render_plan_compiler_detail.hpp"

#include <nlohmann/json.hpp>

namespace {

nlohmann::json plan_with_layer(nlohmann::json layer) {
    return {
        {"schema", "chronon.render-plan.v3"},
        {"version", 3},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps_num", 30},
                    {"fps_den", 1}, {"duration_frames", 12}}},
        {"layers", nlohmann::json::array({std::move(layer)})},
        {"output", {{"path", "out.png"}}},
    };
}

nlohmann::json image_layer() {
    return {
        {"id", "photo"},
        {"type", "image"},
        {"asset", "photo.png"},
        {"size", {120.0, 80.0}},
        {"image_lighting", {
            {"bump_strength", 0.35},
            {"rim_strength", 0.8},
            {"rim_width_px", 6.0},
            {"roughness", 0.2},
            {"specular", 0.4},
            {"gobo_asset", "gobo.png"},
            {"gobo_scale", 0.002},
            {"gobo_offset", {0.25, -0.5}},
            {"light_wrap_asset", "background.png"},
            {"light_wrap_strength", 0.7},
        }},
    };
}

} // namespace

TEST_CASE("ImageLighting RenderPlan: decodes and validates the authored block") {
    auto plan = plan_with_layer(image_layer());
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    if (!decoded) FAIL(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].image_lighting.has_value());
    const auto& lighting = *decoded->layers[0].image_lighting;
    CHECK(lighting.bump_strength == doctest::Approx(0.35f));
    CHECK(lighting.rim_strength == doctest::Approx(0.8f));
    CHECK(lighting.rim_width_px == doctest::Approx(6.0f));
    CHECK(lighting.roughness == doctest::Approx(0.2f));
    CHECK(lighting.specular == doctest::Approx(0.4f));
    CHECK(lighting.gobo_asset == "gobo.png");
    CHECK(lighting.gobo_scale == doctest::Approx(0.002f));
    CHECK(lighting.gobo_offset[0] == doctest::Approx(0.25f));
    CHECK(lighting.gobo_offset[1] == doctest::Approx(-0.5f));
    CHECK(lighting.light_wrap_asset == "background.png");
    CHECK(lighting.light_wrap_strength == doctest::Approx(0.7f));

    plan["layers"][0]["image_lighting"]["bump_strength"] = 4.5;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan).has_value());

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"]["rim_width_px"] = 65.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan).has_value());

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"]["roughness"] = 1.5;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan).has_value());

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"]["unknown"] = 1.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan).has_value());

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"]["gobo_scale"] = 0.2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan).has_value());

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"]["gobo_offset"] = {9.0, 0.0};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan).has_value());

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"].erase("light_wrap_asset");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan).has_value());

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"]["gobo_asset"] = "";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan).has_value());
}

TEST_CASE("ImageLighting RenderPlan: only image and video layers accept the block") {
    auto color_plan = plan_with_layer({
        {"id", "plate"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
        {"image_lighting", {{"bump_strength", 0.2}}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(color_plan).has_value());

    auto video_plan = plan_with_layer({
        {"id", "clip"}, {"type", "video"}, {"source", "clip.mp4"},
        {"image_lighting", {{"rim_strength", 0.5}}}});
    const auto decoded = chronon3d::render_plan::decode_render_plan(video_plan);
    if (!decoded) FAIL(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].image_lighting);
    CHECK(decoded->layers[0].image_lighting->rim_strength == doctest::Approx(0.5f));
}

TEST_CASE("ImageLighting RenderPlan: authored values change the content fingerprint") {
    auto plan = plan_with_layer(image_layer());
    const auto baseline = chronon3d::render_plan::decode_render_plan(plan);
    if (!baseline) FAIL(baseline.error().message);
    REQUIRE(baseline);

    plan["layers"][0]["image_lighting"]["bump_strength"] = 0.6;
    const auto changed_bump = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(changed_bump);
    CHECK(changed_bump->content_fingerprint != baseline->content_fingerprint);

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"]["rim_width_px"] = 9.0;
    const auto changed_width = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(changed_width);
    CHECK(changed_width->content_fingerprint != baseline->content_fingerprint);

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"].erase("roughness");
    const auto no_roughness = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(no_roughness);
    CHECK(no_roughness->content_fingerprint != baseline->content_fingerprint);

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"]["gobo_asset"] = "other-gobo.png";
    const auto changed_gobo = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(changed_gobo);
    CHECK(changed_gobo->content_fingerprint != baseline->content_fingerprint);

    plan = plan_with_layer(image_layer());
    plan["layers"][0]["image_lighting"]["light_wrap_strength"] = 0.9;
    const auto changed_wrap = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(changed_wrap);
    CHECK(changed_wrap->content_fingerprint != baseline->content_fingerprint);
}

TEST_CASE("ImageLighting RenderPlan: compiler lowers the block onto Material2_5D") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;

    LayerPlan layer;
    layer.id = "photo";
    layer.type = LayerType::Image;
    layer.size_dimensions = 2;
    layer.size = {120.0f, 80.0f};
    ImageLightingPlan lighting;
    lighting.bump_strength = 0.35f;
    lighting.rim_strength = 0.8f;
    lighting.rim_width_px = 6.0f;
    lighting.roughness = 0.2f;
    lighting.specular = 0.4f;
    lighting.gobo_asset = "gobo.png";
    lighting.gobo_scale = 0.002f;
    lighting.gobo_offset = {0.25f, -0.5f};
    lighting.light_wrap_asset = "background.png";
    lighting.light_wrap_strength = 0.7f;
    layer.image_lighting = lighting;

    RenderPlan plan;
    LayerBuilder builder("photo", Frame{0}, FrameRate{30, 1});
    chronon3d::render_plan::detail::append_image_layer_for_scene(
        builder, layer, plan, "");
    const auto built = builder.build();

    CHECK(built.material().image_bump_strength == doctest::Approx(0.35f));
    CHECK(built.material().image_rim_strength == doctest::Approx(0.8f));
    CHECK(built.material().image_rim_width_px == doctest::Approx(6.0f));
    REQUIRE(built.material().roughness.has_value());
    CHECK(*built.material().roughness == doctest::Approx(0.2f));
    CHECK(built.material().specular == doctest::Approx(0.4f));
    CHECK(built.material().image_gobo_asset == "gobo.png");
    CHECK(built.material().image_gobo_scale == doctest::Approx(0.002f));
    CHECK(built.material().image_gobo_offset.x == doctest::Approx(0.25f));
    CHECK(built.material().image_gobo_offset.y == doctest::Approx(-0.5f));
    CHECK(built.material().image_light_wrap_asset == "background.png");
    CHECK(built.material().image_light_wrap_strength == doctest::Approx(0.7f));
}
