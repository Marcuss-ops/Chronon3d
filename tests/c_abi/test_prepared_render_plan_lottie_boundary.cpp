#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace {
class TempDirectory {
public:
    TempDirectory() {
        path = std::filesystem::temp_directory_path() /
            ("chronon3d_lottie_boundary_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path / "shapes");
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};
} // namespace

TEST_CASE("Lottie imports multiple layers and nested precompositions") {
    using namespace chronon3d::render_plan;
    TempDirectory directory;
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"shape","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/mark.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto shape_layer = [](int index, bool red, int x) {
        return nlohmann::json{
            {"ddd", 0}, {"ind", index}, {"ty", 4},
            {"ks", {{"a", {{"a", 0}, {"k", {0, 0, 0}}}},
                    {"p", {{"a", 0}, {"k", {x, 20, 0}}}},
                    {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
                    {"r", {{"a", 0}, {"k", 0}}},
                    {"o", {{"a", 0}, {"k", 100}}}}},
            {"shapes", nlohmann::json::array({
                {{"ty", "sh"}, {"ks", {{"a", 0}, {"k", {
                    {"c", true}, {"v", {{0, 0}, {24, 0}, {24, 24}, {0, 24}}},
                    {"i", {{0, 0}, {0, 0}, {0, 0}, {0, 0}}},
                    {"o", {{0, 0}, {0, 0}, {0, 0}, {0, 0}}}}}}}},
                {{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", red
                    ? nlohmann::json::array({1, 0, 0, 1}) : nlohmann::json::array({0, 0, 1, 1})}}},
                 {"o", {{"a", 0}, {"k", 100}}}}
            })}
        };
    };
    {
        std::ofstream file(directory.path / "shapes/mark.json", std::ios::trunc);
        nlohmann::json document{{"v", "5.7.4"}, {"ip", 0}, {"op", 10}, {"fr", 30},
            {"layers", nlohmann::json::array({
                shape_layer(1, true, 12), shape_layer(2, false, 28)})}};
        document["layers"][0]["parent"] = 2;
        file << document;
    }
    const auto prepared = compile_render_plan(*decoded, resolver);
    const std::string error = prepared ? std::string{} : prepared.error().message;
    REQUIRE_MESSAGE(prepared, error);
    const auto evaluated = chronon3d::evaluate(prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    const std::string evaluation_error = evaluated ? std::string{} : evaluated.error().message;
    REQUIRE_MESSAGE(evaluated, evaluation_error);
    REQUIRE(evaluated->scene.layers().size() == 2);
    CHECK(evaluated->scene.layers()[0].name == "shape__lottie_1");
    CHECK(evaluated->scene.layers()[1].name == "shape__lottie_0");
    CHECK(evaluated->scene.layers()[1].parent_name == "shape__lottie_1");

    {
        std::ofstream file(directory.path / "shapes/mark.json", std::ios::trunc);
        const auto identity_transform = nlohmann::json{
            {"a", {{"a", 0}, {"k", {0, 0, 0}}}},
            {"p", {{"a", 0}, {"k", {32, 32, 0}}}},
            {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
            {"r", {{"a", 0}, {"k", 0}}},
            {"o", {{"a", 0}, {"k", 100}}}};
        const nlohmann::json nested_precomp{
            {"ty", 0}, {"ind", 2}, {"refId", "leaf"}, {"ip", 0}, {"op", 10},
            {"st", 1}, {"sr", 1}, {"ks", identity_transform}};
        nlohmann::json document{
            {"v", "5.7.4"}, {"ip", 0}, {"op", 10}, {"fr", 30},
            {"w", 64}, {"h", 64},
            {"layers", nlohmann::json::array({{{"ty", 0}, {"ind", 1},
                {"refId", "nested"}, {"ip", 0}, {"op", 10}, {"st", 0}, {"sr", 1},
                {"ks", identity_transform}}})},
            {"assets", nlohmann::json::array({
                {{"id", "nested"}, {"w", 64}, {"h", 64},
                 {"layers", nlohmann::json::array({nested_precomp})}},
                {{"id", "leaf"}, {"w", 64}, {"h", 64},
                 {"layers", nlohmann::json::array({shape_layer(11, true, 8)})}}
            })}
        };
        file << document;
    }
    const auto precomp = compile_render_plan(*decoded, resolver);
    const std::string precomp_error = precomp ? std::string{} : precomp.error().message;
    REQUIRE_MESSAGE(precomp, precomp_error);
    const auto nested = chronon3d::evaluate(precomp->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(nested);
    REQUIRE(nested->scene.layers().size() == 3);
    CHECK(nested->scene.layers()[1].parent_name == nested->scene.layers()[0].name);
    CHECK(nested->scene.layers()[2].parent_name == nested->scene.layers()[1].name);

    {
        std::ofstream file(directory.path / "shapes/mark.json", std::ios::trunc);
        file << R"JSON({"v":"5.7.4","ip":0,"op":10,"layers":[{"ty":0,"ind":1,"refId":"nested","ip":0,"op":10,"ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}}}],"assets":[{"id":"nested","layers":[{"ty":0,"ind":2,"refId":"nested","ip":0,"op":10,"ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}}}]}]})JSON";
    }
    const auto cyclic = compile_render_plan(*decoded, resolver);
    CHECK_FALSE(cyclic);
    if (!cyclic) CHECK(cyclic.error().message.find("cycle") != std::string::npos);
}

TEST_CASE("Lottie shape track mattes lower to canonical render-plan relations") {
    using namespace chronon3d::render_plan;
    using chronon3d::TrackMatteType;
    TempDirectory directory;
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":10},
      "layers":[{"id":"shape","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/matte.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);

    const auto shape_layer = [](int index, int matte_type = 0) {
        nlohmann::json layer{
            {"ddd", 0}, {"ind", index}, {"ty", 4},
            {"ks", {{"a", {{"a", 0}, {"k", {0, 0, 0}}}},
                    {"p", {{"a", 0}, {"k", {32, 32, 0}}}},
                    {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
                    {"r", {{"a", 0}, {"k", 0}}},
                    {"o", {{"a", 0}, {"k", 100}}}}},
            {"shapes", nlohmann::json::array({
                {{"ty", "sh"}, {"ks", {{"a", 0}, {"k", {
                    {"c", true}, {"v", {{0, 0}, {20, 0}, {20, 20}, {0, 20}}},
                    {"i", {{0, 0}, {0, 0}, {0, 0}, {0, 0}}},
                    {"o", {{0, 0}, {0, 0}, {0, 0}, {0, 0}}}}}}}},
                {{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {1, 1, 1, 1}}}},
                 {"o", {{"a", 0}, {"k", 100}}}}
            })}
        };
        if (matte_type != 0) layer["tt"] = matte_type;
        return layer;
    };
    constexpr std::array expected{
        TrackMatteType::Alpha, TrackMatteType::AlphaInverted,
        TrackMatteType::Luma, TrackMatteType::LumaInverted};
    for (int type = 1; type <= 4; ++type) {
        for (const bool explicit_source : {false, true}) {
            auto target = shape_layer(20, type);
            if (explicit_source) target["tp"] = 10;
            nlohmann::json document{
                {"v", "5.7.4"}, {"ip", 0}, {"op", 10}, {"fr", 30},
                {"layers", nlohmann::json::array({
                    shape_layer(10), std::move(target)})}};
            {
                std::ofstream file(directory.path / "shapes/matte.json", std::ios::trunc);
                file << document;
            }
            const auto prepared = compile_render_plan(*decoded, resolver);
            const std::string error = prepared ? std::string{} : prepared.error().message;
            REQUIRE_MESSAGE(prepared, error);
            const auto evaluated = chronon3d::evaluate(prepared->compiled_composition,
                chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
            REQUIRE(evaluated);
            const auto target_layer = std::find_if(evaluated->scene.layers().begin(),
                evaluated->scene.layers().end(), [](const auto& layer) {
                    return layer.track_matte.active();
                });
            REQUIRE(target_layer != evaluated->scene.layers().end());
            CHECK(target_layer->track_matte.type == expected[static_cast<std::size_t>(type - 1)]);
            CHECK(target_layer->track_matte.source_layer == "shape__lottie_0");
        }
    }

    const auto compile_document = [&](nlohmann::json document) {
        std::ofstream file(directory.path / "shapes/matte.json", std::ios::trunc);
        file << document;
        file.close();
        return compile_render_plan(*decoded, resolver);
    };
    CHECK_FALSE(compile_document({{"v", "5.7.4"}, {"ip", 0}, {"op", 10},
        {"layers", nlohmann::json::array({shape_layer(20, 1)})}}));
    auto missing_explicit_source = shape_layer(20, 1);
    missing_explicit_source["tp"] = 999;
    CHECK_FALSE(compile_document({{"v", "5.7.4"}, {"ip", 0}, {"op", 10},
        {"layers", nlohmann::json::array({shape_layer(10), missing_explicit_source})}}));
    CHECK_FALSE(compile_document({{"v", "5.7.4"}, {"ip", 0}, {"op", 10},
        {"layers", nlohmann::json::array({shape_layer(10), shape_layer(20, 5)})}}));
    auto hidden_source = shape_layer(10);
    hidden_source["hd"] = true;
    CHECK_FALSE(compile_document({{"v", "5.7.4"}, {"ip", 0}, {"op", 10},
        {"layers", nlohmann::json::array({hidden_source, shape_layer(20, 1)})}}));
    auto non_shape_source = shape_layer(10);
    non_shape_source["ty"] = 1;
    CHECK_FALSE(compile_document({{"v", "5.7.4"}, {"ip", 0}, {"op", 10},
        {"layers", nlohmann::json::array({non_shape_source, shape_layer(20, 1)})}}));
    auto cycle_a = shape_layer(10, 1);
    auto cycle_b = shape_layer(20, 1);
    cycle_a["tp"] = 20;
    cycle_b["tp"] = 10;
    const auto cyclic_mattes = compile_document({{"v", "5.7.4"}, {"ip", 0}, {"op", 10},
        {"layers", nlohmann::json::array({cycle_a, cycle_b})}});
    CHECK_FALSE(cyclic_mattes);
    if (!cyclic_mattes) {
        CHECK(cyclic_mattes.error().code == "layer_dependency_cycle");
    }
}

TEST_CASE("Lottie image layers resolve external assets through the mounted resolver") {
    using namespace chronon3d::render_plan;
    TempDirectory directory;
    std::filesystem::create_directories(directory.path / "shapes/images");
    constexpr std::array<std::uint8_t, 68> pixel_png{
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x04,0x00,0x00,0x00,0xb5,0x1c,0x0c,
        0x02,0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0xfc,0xff,0x1f,0x00,
        0x03,0x03,0x02,0x00,0xef,0xbf,0xe5,0xea,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,
        0xae,0x42,0x60,0x82};
    {
        std::ofstream image(directory.path / "shapes/images/pixel.png", std::ios::binary);
        image.write(reinterpret_cast<const char*>(pixel_png.data()),
                    static_cast<std::streamsize>(pixel_png.size()));
    }
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":10},
      "layers":[{"id":"shape","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/image.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const nlohmann::json transform{
        {"a", {{"a", 0}, {"k", {0, 0, 0}}}},
        {"p", {{"a", 0}, {"k", {32, 32, 0}}}},
        {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
        {"r", {{"a", 0}, {"k", 0}}},
        {"o", {{"a", 0}, {"k", 100}}}};
    const nlohmann::json image_layer{
        {"ty", 2}, {"ind", 1}, {"refId", "pixel"}, {"ip", 0}, {"op", 10},
        {"ks", transform}};
    {
        std::ofstream file(directory.path / "shapes/image.json", std::ios::trunc);
        file << nlohmann::json{
            {"v", "5.7.4"}, {"ip", 0}, {"op", 10}, {"fr", 30},
            {"w", 64}, {"h", 64},
            {"layers", nlohmann::json::array({image_layer})},
            {"assets", nlohmann::json::array({
                {{"id", "pixel"}, {"p", "pixel.png"}, {"u", "images/"},
                 {"e", 0}, {"w", 1}, {"h", 1}}})}};
    }
    const auto prepared = compile_render_plan(*decoded, resolver);
    const std::string error = prepared ? std::string{} : prepared.error().message;
    REQUIRE_MESSAGE(prepared, error);
    const auto evaluated = chronon3d::evaluate(prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(evaluated);
    CHECK(evaluated->scene.layers().size() == 1);

    auto effected_image_layer = image_layer;
    effected_image_layer["ef"] = nlohmann::json::array({
        {{"ty", 29}, {"en", 1}, {"ef", nlohmann::json::array({
            {{"v", {{"a", 0}, {"k", 12}}}},
            {{"v", {{"a", 0}, {"k", 1}}}},
            {{"v", {{"a", 0}, {"k", 0}}}}
        })}},
        {{"ty", 25}, {"en", 1}, {"ef", nlohmann::json::array({
            {{"v", {{"a", 0}, {"k", {0.2f, 0.3f, 0.4f, 1.0f}}}}},
            {{"v", {{"a", 0}, {"k", 128}}}},
            {{"v", {{"a", 0}, {"k", 90}}}},
            {{"v", {{"a", 0}, {"k", 10}}}},
            {{"v", {{"a", 0}, {"k", 20}}}},
            {{"v", {{"a", 0}, {"k", 0}}}}
        })}},
        {{"ty", 21}, {"en", 1}, {"ef", nlohmann::json::array({
            {{"v", {{"a", 0}, {"k", 0}}}}, {{"v", {{"a", 0}, {"k", 0}}}},
            {{"v", {{"a", 0}, {"k", {1.0f, 0.0f, 0.0f, 1.0f}}}}},
            {{"v", {{"a", 0}, {"k", 0}}}}, {{"v", {{"a", 0}, {"k", 0}}}},
            {{"v", {{"a", 0}, {"k", 0}}}}, {{"v", {{"a", 0}, {"k", 50}}}}
        })}}
    });
    const auto write_image_document = [&](const nlohmann::json& layer) {
        std::ofstream file(directory.path / "shapes/image.json", std::ios::trunc);
        file << nlohmann::json{
            {"v", "5.7.4"}, {"ip", 0}, {"op", 10}, {"fr", 30},
            {"w", 64}, {"h", 64},
            {"layers", nlohmann::json::array({layer})},
            {"assets", nlohmann::json::array({
                {{"id", "pixel"}, {"p", "pixel.png"}, {"u", "images/"},
                 {"e", 0}, {"w", 1}, {"h", 1}}})}};
    };
    write_image_document(effected_image_layer);
    const auto effected = compile_render_plan(*decoded, resolver);
    const std::string effected_error = effected ? std::string{} : effected.error().message;
    REQUIRE_MESSAGE(effected, effected_error);
    const auto effected_scene = chronon3d::evaluate(effected->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(effected_scene);
    const auto& lowered_effects = effected_scene->scene.layers()[0].effects();
    REQUIRE(lowered_effects.size() == 3);
    const auto* blur = std::get_if<chronon3d::BlurParams>(&lowered_effects[0].params);
    REQUIRE(blur);
    CHECK(blur->radius == doctest::Approx(3.6f));
    const auto* shadow = std::get_if<chronon3d::DropShadowParams>(&lowered_effects[1].params);
    REQUIRE(shadow);
    CHECK(shadow->offset.x == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(shadow->offset.y == doctest::Approx(10.0f));
    CHECK(shadow->radius == doctest::Approx(6.0f));
    CHECK(shadow->color.a == doctest::Approx(128.0f / 255.0f));
    const auto* fill = std::get_if<chronon3d::FillParams>(&lowered_effects[2].params);
    REQUIRE(fill);
    CHECK(fill->amount == doctest::Approx(0.5f));
    CHECK(fill->mode == chronon3d::FillMode::ReplaceAlpha);
    CHECK(fill->color.r == doctest::Approx(1.0f));

    auto scaled_effect_layer = effected_image_layer;
    scaled_effect_layer["ks"]["s"]["k"] = {50, 50, 100};
    write_image_document(scaled_effect_layer);
    const auto scaled_effect = compile_render_plan(*decoded, resolver);
    REQUIRE(scaled_effect);
    const auto scaled_effect_scene = chronon3d::evaluate(scaled_effect->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(scaled_effect_scene);
    const auto& scaled_effects = scaled_effect_scene->scene.layers()[0].effects();
    REQUIRE(scaled_effects.size() == 3);
    const auto* scaled_blur = std::get_if<chronon3d::BlurParams>(&scaled_effects[0].params);
    const auto* scaled_shadow = std::get_if<chronon3d::DropShadowParams>(&scaled_effects[1].params);
    REQUIRE(scaled_blur);
    REQUIRE(scaled_shadow);
    CHECK(scaled_blur->radius == doctest::Approx(1.8f));
    CHECK(scaled_shadow->radius == doctest::Approx(3.0f));
    CHECK(scaled_shadow->offset.y == doctest::Approx(5.0f));

    auto vertical_blur_layer = effected_image_layer;
    vertical_blur_layer["ef"][0]["ef"][1]["v"]["k"] = 3;
    write_image_document(vertical_blur_layer);
    const auto rejected_vertical_blur = compile_render_plan(*decoded, resolver);
    CHECK_FALSE(rejected_vertical_blur);
    CHECK(rejected_vertical_blur.error().message.find("both dimensions (direction 1)") !=
          std::string::npos);

    auto animated_fill_layer = image_layer;
    animated_fill_layer["ef"] = nlohmann::json::array({
        {{"ty", 21}, {"en", 1}, {"ef", nlohmann::json::array({
            {{"v", {{"a", 0}, {"k", 0}}}}, {{"v", {{"a", 0}, {"k", 0}}}},
            {{"v", {{"a", 1}, {"k", nlohmann::json::array({
                {{"t", 0}, {"s", {1.0f, 0.0f, 0.0f, 1.0f}}},
                {{"t", 9}, {"s", {0.0f, 0.0f, 1.0f, 1.0f}}}
            })}}}},
            {{"v", {{"a", 0}, {"k", 0}}}}, {{"v", {{"a", 0}, {"k", 0}}}},
            {{"v", {{"a", 0}, {"k", 0}}}},
            {{"v", {{"a", 1}, {"k", nlohmann::json::array({
                {{"t", 0}, {"s", 25.0f}}, {{"t", 9}, {"s", 100.0f}}
            })}}}}
        })}}
    });
    write_image_document(animated_fill_layer);
    const auto animated_fill = compile_render_plan(*decoded, resolver);
    const std::string animated_fill_error = animated_fill ? std::string{} : animated_fill.error().message;
    REQUIRE_MESSAGE(animated_fill, animated_fill_error);
    const auto fill_at_start = chronon3d::evaluate(animated_fill->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(fill_at_start);
    const auto* start_fill = std::get_if<chronon3d::FillParams>(
        &fill_at_start->scene.layers()[0].effects()[0].params);
    REQUIRE(start_fill);
    CHECK(start_fill->color.r == doctest::Approx(1.0f));
    CHECK(start_fill->color.b == doctest::Approx(0.0f));
    CHECK(start_fill->amount == doctest::Approx(0.25f));
    const auto fill_at_end = chronon3d::evaluate(animated_fill->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{9});
    REQUIRE(fill_at_end);
    const auto* end_fill = std::get_if<chronon3d::FillParams>(
        &fill_at_end->scene.layers()[0].effects()[0].params);
    REQUIRE(end_fill);
    const auto& fill_tracks = fill_at_end->scene.layers()[0].effects()[0].parameter_tracks;
    REQUIRE(fill_tracks.size() == 4);
    const auto find_track = [](const auto& tracks, std::string_view name) -> const auto* {
        const auto found = std::find_if(tracks.begin(), tracks.end(), [&](const auto& track) {
            return track.param == name;
        });
        return found == tracks.end() ? nullptr : &*found;
    };
    const auto* red_track = find_track(fill_tracks, "color.r");
    const auto* blue_track = find_track(fill_tracks, "color.b");
    const auto* fill_amount_track = find_track(fill_tracks, "amount");
    REQUIRE(red_track);
    REQUIRE(blue_track);
    REQUIRE(fill_amount_track);
    CHECK(red_track->keyframes.back().value == doctest::Approx(0.0f));
    CHECK(blue_track->keyframes.back().value == doctest::Approx(1.0f));
    CHECK(fill_amount_track->keyframes.back().value == doctest::Approx(1.0f));

    auto animated_effect = effected_image_layer;
    animated_effect["ef"][0]["ef"][0]["v"] = {
        {"a", 1}, {"k", {{{"t", 0}, {"s", 0.0f}}, {{"t", 9}, {"s", 30.0f}}}}};
    animated_effect["ef"][1]["ef"][4]["v"] = {
        {"a", 1}, {"k", {{{"t", 0}, {"s", 0.0f}}, {{"t", 9}, {"s", 20.0f}}}}};
    write_image_document(animated_effect);
    const auto animated_blurs = compile_render_plan(*decoded, resolver);
    const std::string animated_blurs_error = animated_blurs
        ? std::string{} : animated_blurs.error().message;
    REQUIRE_MESSAGE(animated_blurs, animated_blurs_error);
    const auto blur_start = chronon3d::evaluate(animated_blurs->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(blur_start);
    CHECK(std::get<chronon3d::BlurParams>(blur_start->scene.layers()[0].effects()[0].params)
        .radius == doctest::Approx(0.0f));
    CHECK(std::get<chronon3d::DropShadowParams>(blur_start->scene.layers()[0].effects()[1].params)
        .radius == doctest::Approx(0.0f));
    const auto blur_end = chronon3d::evaluate(animated_blurs->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{9});
    REQUIRE(blur_end);
    const auto& blur_tracks = blur_end->scene.layers()[0].effects()[0].parameter_tracks;
    const auto& shadow_tracks = blur_end->scene.layers()[0].effects()[1].parameter_tracks;
    REQUIRE(blur_tracks.size() == 1);
    REQUIRE(shadow_tracks.size() == 1);
    CHECK(blur_tracks[0].param == "radius");
    CHECK(blur_tracks[0].keyframes.back().value == doctest::Approx(9.0f));
    CHECK(shadow_tracks[0].param == "radius");
    CHECK(shadow_tracks[0].keyframes.back().value == doctest::Approx(6.0f));

    animated_effect["ef"][0]["ef"][0]["v"]["k"][1]["s"] = 1000.0f;
    write_image_document(animated_effect);
    CHECK_FALSE(compile_render_plan(*decoded, resolver));

    auto write_asset_path = [&](std::string path, std::string subdirectory = "images/") {
        std::ofstream file(directory.path / "shapes/image.json", std::ios::trunc);
        file << nlohmann::json{
            {"v", "5.7.4"}, {"ip", 0}, {"op", 10}, {"fr", 30},
            {"layers", nlohmann::json::array({image_layer})},
            {"assets", nlohmann::json::array({
                {{"id", "pixel"}, {"p", std::move(path)}, {"u", std::move(subdirectory)},
                 {"e", 0}, {"w", 1}, {"h", 1}}})}};
    };
    write_asset_path("missing.png");
    CHECK_FALSE(compile_render_plan(*decoded, resolver));
    write_asset_path("pixel.png", "../../outside/");
    CHECK_FALSE(compile_render_plan(*decoded, resolver));

    {
        std::ofstream file(directory.path / "shapes/image.json", std::ios::trunc);
        auto embedded = nlohmann::json{
            {"v", "5.7.4"}, {"ip", 0}, {"op", 10}, {"fr", 30},
            {"layers", nlohmann::json::array({image_layer})},
            {"assets", nlohmann::json::array({
                {{"id", "pixel"}, {"p", "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABQAAAAKCAYAAAC0VX7mAAAAGklEQVR4nGP4z8DwnxgMRsTgUQNHDRyOBgIAdxAq5F9kDv4AAAAASUVORK5CYII="},
                 {"e", 1}, {"w", 1}, {"h", 1}}})}};
        file << embedded;
    }
    const auto embedded_plan = compile_render_plan(*decoded, resolver);
    const std::string embedded_error = embedded_plan ? std::string{} : embedded_plan.error().message;
    REQUIRE_MESSAGE(embedded_plan, embedded_error);
    const auto embedded_scene = chronon3d::evaluate(
        embedded_plan->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(embedded_scene);
    REQUIRE(embedded_scene->scene.layers().size() == 1);
    REQUIRE(embedded_scene->scene.layers()[0].nodes.size() == 1);
    const auto& embedded_image =
        embedded_scene->scene.layers()[0].nodes[0].shape.image();
    CHECK(embedded_image.path.starts_with("chronon3d-embedded:"));
    REQUIRE(embedded_image.embedded_payload);
    CHECK(embedded_image.embedded_payload->size() > 32);

    {
        std::ofstream file(directory.path / "shapes/image.json", std::ios::trunc);
        auto malformed = nlohmann::json{
            {"v", "5.7.4"}, {"ip", 0}, {"op", 10}, {"fr", 30},
            {"layers", nlohmann::json::array({image_layer})},
            {"assets", nlohmann::json::array({
                {{"id", "pixel"}, {"p", "data:image/png;base64,AA=="},
                 {"e", 1}, {"w", 1}, {"h", 1}}})}};
        file << malformed;
    }
    CHECK_FALSE(compile_render_plan(*decoded, resolver));
}

TEST_CASE("Lottie static text layers lower to Chronon's native text engine") {
    using namespace chronon3d::render_plan;
    TempDirectory directory;
    std::filesystem::create_directories(directory.path / "shapes/fonts");
    std::filesystem::copy_file(
        std::filesystem::path(CHRONON3D_TEST_SOURCE_DIR) /
            "golden/lottie_cli_gate/fonts/Inter-Regular.ttf",
        directory.path / "shapes/fonts/Inter-Regular.ttf",
        std::filesystem::copy_options::overwrite_existing);
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":10},
      "layers":[{"id":"shape","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/text.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const nlohmann::json transform{
        {"a", {{"a", 0}, {"k", {0, 0, 0}}}},
        {"p", {{"a", 0}, {"k", {32, 32, 0}}}},
        {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
        {"r", {{"a", 0}, {"k", 0}}},
        {"o", {{"a", 0}, {"k", 100}}}};
    const auto text_layer = [&] {
        return nlohmann::json{
            {"ty", 5}, {"ind", 1}, {"ip", 0}, {"op", 10}, {"ddd", 0},
            {"ks", transform},
            {"t", {
                {"d", {{"k", nlohmann::json::array({
                    {{"s", {{"s", 20}, {"f", "Inter-Regular"},
                        {"t", "Chronon\nThorVG"}, {"j", 0}, {"tr", 12},
                        {"fc", {0.2, 0.4, 0.8, 0.75}}}}, {"t", 0}}})}}},
                {"a", nlohmann::json::array()}}}};
    };
    const auto write_document = [&](const nlohmann::json& layer) {
        std::ofstream file(directory.path / "shapes/text.json", std::ios::trunc);
        file << nlohmann::json{
            {"v", "5.7.4"}, {"ip", 0}, {"op", 10}, {"fr", 30},
            {"fonts", {{"list", nlohmann::json::array({
                {{"fName", "Inter-Regular"}, {"fFamily", "Inter"},
                 {"fStyle", "Regular"}, {"fPath", "fonts/Inter-Regular.ttf"}}})}}},
            {"layers", nlohmann::json::array({layer})}};
    };
    write_document(text_layer());
    const auto prepared = compile_render_plan(*decoded, resolver);
    const std::string error = prepared ? std::string{} : prepared.error().message;
    REQUIRE_MESSAGE(prepared, error);
    const auto evaluated = chronon3d::evaluate(prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    const std::string evaluation_error = evaluated ? std::string{} : evaluated.error().message;
    REQUIRE_MESSAGE(evaluated, evaluation_error);
    REQUIRE(evaluated->scene.layers().size() == 1);
    CHECK(evaluated->scene.layers()[0].is_text());

    auto animated = text_layer();
    animated["t"]["d"]["k"][0]["h"] = 1;
    animated["t"]["d"]["k"].push_back({
        {"s", {{"s", 20}, {"f", "Inter-Regular"}, {"t", "Changed"},
            {"j", 0}, {"fc", {1, 1, 1}}}}, {"t", 5}});
    write_document(animated);
    const auto animated_prepared = compile_render_plan(*decoded, resolver);
    const std::string animated_error = animated_prepared
        ? std::string{} : animated_prepared.error().message;
    REQUIRE_MESSAGE(animated_prepared, animated_error);
    const auto first_document_scene = chronon3d::evaluate(
        animated_prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    const auto second_document_scene = chronon3d::evaluate(
        animated_prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{5});
    REQUIRE(first_document_scene);
    REQUIRE(second_document_scene);
    REQUIRE(first_document_scene->scene.layers().size() == 2);
    REQUIRE(second_document_scene->scene.layers().size() == 2);
    CHECK(first_document_scene->scene.layers()[0].is_text());
    CHECK(first_document_scene->scene.layers()[1].is_text());
    CHECK(first_document_scene->scene.layers()[0].from == chronon3d::Frame{0});
    CHECK(first_document_scene->scene.layers()[0].duration == chronon3d::Frame{5});
    CHECK(first_document_scene->scene.layers()[1].from == chronon3d::Frame{5});
    CHECK(first_document_scene->scene.layers()[1].duration == chronon3d::Frame{5});
}

TEST_CASE("Lottie precomp time-stretch remaps layer windows and transform keys") {
    using namespace chronon3d::render_plan;
    TempDirectory directory;
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"shape","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/stretch.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const nlohmann::json identity_transform{
        {"a", {{"a", 0}, {"k", {0, 0, 0}}}},
        {"p", {{"a", 0}, {"k", {0, 0, 0}}}},
        {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
        {"r", {{"a", 0}, {"k", 0}}},
        {"o", {{"a", 0}, {"k", 100}}}};
    const auto shape = [&] {
        auto layer = nlohmann::json{
            {"ty", 4}, {"ind", 11}, {"ip", 0}, {"op", 10}, {"ddd", 0},
            {"ks", identity_transform},
            {"shapes", nlohmann::json::array({
                {{"ty", "sh"}, {"ks", {{"a", 0}, {"k", {
                    {"c", true}, {"v", {{0, 0}, {16, 0}, {16, 16}, {0, 16}}},
                    {"i", {{0, 0}, {0, 0}, {0, 0}, {0, 0}}},
                    {"o", {{0, 0}, {0, 0}, {0, 0}, {0, 0}}}}}}}},
                {{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {1, 0, 0, 1}}}},
                 {"o", {{"a", 0}, {"k", 100}}}}})}};
        layer["ks"]["p"] = {{"a", 1}, {"k", nlohmann::json::array({
            {{"t", 0}, {"s", {8, 20, 0}}},
            {{"t", 10}, {"s", {18, 20, 0}}}})}};
        return layer;
    };
    const auto write_document = [&](double stretch) {
        std::ofstream file(directory.path / "shapes/stretch.json", std::ios::trunc);
        file << nlohmann::json{
            {"v", "5.7.4"}, {"ip", 0}, {"op", 30}, {"fr", 30},
            {"layers", nlohmann::json::array({
                {{"ty", 0}, {"ind", 1}, {"refId", "comp"},
                 {"ip", 0}, {"op", 30}, {"st", 0}, {"sr", stretch},
                 {"ks", identity_transform}}})},
            {"assets", nlohmann::json::array({
                {{"id", "comp"}, {"w", 64}, {"h", 64}, {"layers", {shape()}}}})}};
    };
    const auto child_at = [&](double stretch, std::int64_t frame) {
        write_document(stretch);
        const auto prepared = compile_render_plan(*decoded, resolver);
        const std::string error = prepared ? std::string{} : prepared.error().message;
        REQUIRE_MESSAGE(prepared, error);
        const auto evaluated = chronon3d::evaluate(prepared->compiled_composition,
            chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{frame});
        const std::string evaluation_error = evaluated ? std::string{} : evaluated.error().message;
        REQUIRE_MESSAGE(evaluated, evaluation_error);
        const auto found = std::find_if(evaluated->scene.layers().begin(),
            evaluated->scene.layers().end(), [](const auto& layer) {
                return layer.name == "shape__lottie_1";
            });
        REQUIRE(found != evaluated->scene.layers().end());
        return std::pair{found->duration.integral(), found->transform.position.x};
    };
    const auto half_speed = child_at(2.0, 10);
    CHECK(half_speed.first == 20);
    CHECK(half_speed.second == doctest::Approx(-19.0f));
    const auto double_speed = child_at(0.5, 2);
    CHECK(double_speed.first == 5);
    CHECK(double_speed.second == doctest::Approx(-20.0f));
    write_document(0.25);
    const auto fractional = compile_render_plan(*decoded, resolver);
    CHECK_FALSE(fractional);

    {
        std::ofstream file(directory.path / "shapes/stretch.json", std::ios::trunc);
        file << nlohmann::json{
            {"v", "5.7.4"}, {"ip", 0}, {"op", 30}, {"fr", 30},
            {"layers", nlohmann::json::array({
                {{"ty", 0}, {"ind", 1}, {"refId", "outer"}, {"ip", 0},
                 {"op", 30}, {"st", 0}, {"sr", 2}, {"ks", identity_transform}}})},
            {"assets", nlohmann::json::array({
                {{"id", "outer"}, {"w", 64}, {"h", 64}, {"layers",
                    nlohmann::json::array({{{"ty", 0}, {"ind", 2}, {"refId", "inner"},
                        {"ip", 0}, {"op", 10}, {"st", 0}, {"sr", 0.5},
                        {"ks", identity_transform}}})}},
                {{"id", "inner"}, {"w", 64}, {"h", 64}, {"layers", {shape()}}}})}};
    }
    const auto nested_prepared = compile_render_plan(*decoded, resolver);
    const std::string nested_error = nested_prepared
        ? std::string{} : nested_prepared.error().message;
    REQUIRE_MESSAGE(nested_prepared, nested_error);
    const auto nested_evaluated = chronon3d::evaluate(nested_prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{5});
    REQUIRE(nested_evaluated);
    const auto nested_child = std::find_if(nested_evaluated->scene.layers().begin(),
        nested_evaluated->scene.layers().end(), [](const auto& layer) {
            return layer.name == "shape__lottie_2";
        });
    REQUIRE(nested_child != nested_evaluated->scene.layers().end());
    CHECK(nested_child->duration == chronon3d::Frame{10});
    CHECK(nested_child->transform.position.x == doctest::Approx(-19.0f));
}

TEST_CASE("Lottie malformed documents and resource bounds fail with typed errors") {
    using namespace chronon3d::render_plan;
    using nlohmann::json;
    TempDirectory directory;
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"shape","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/bounded.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto path = directory.path / "shapes/bounded.json";
    const auto write = [&](const std::string& text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        REQUIRE(file.good());
        return compile_render_plan(*decoded, resolver);
    };
    const auto transform = json{
        {"a", {{"a", 0}, {"k", {0, 0, 0}}}},
        {"p", {{"a", 0}, {"k", {32, 32, 0}}}},
        {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
        {"r", {{"a", 0}, {"k", 0}}},
        {"o", {{"a", 0}, {"k", 100}}}};
    const auto shape_layer = [&] {
        return json{{"ty", 4}, {"ind", 1}, {"ddd", 0}, {"ip", 0}, {"op", 10},
            {"ks", transform}, {"shapes", json::array({
                {{"ty", "sh"}, {"ks", {{"a", 0}, {"k", {
                    {"c", true}, {"v", {{0, 0}, {8, 0}, {8, 8}, {0, 8}}},
                    {"i", {{0, 0}, {0, 0}, {0, 0}, {0, 0}}},
                    {"o", {{0, 0}, {0, 0}, {0, 0}, {0, 0}}}}}}}},
                {{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {1, 0, 0, 1}}}}}
            })}};
    };
    const auto root = [&](json layers, json assets = json::array()) {
        return json{{"v", "5.7.4"}, {"ip", 0}, {"op", 12}, {"fr", 30},
                    {"w", 64}, {"h", 64}, {"layers", std::move(layers)},
                    {"assets", std::move(assets)}};
    };
    const auto expect_rejected = [&](const std::string& input,
                                    std::string_view message_fragment) {
        auto result = write(input);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == "lottie_unsupported_or_invalid");
        CHECK(result.error().message.find(message_fragment) != std::string::npos);
    };

    // A syntactically malformed source must return a typed importer error.
    expect_rejected("{ this is not JSON", "parse");

    // The resolver boundary rejects absent files with its distinct typed error.
    std::filesystem::remove(path);
    auto missing = compile_render_plan(*decoded, resolver);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == "lottie_asset_unresolved");

    // Expression payloads are rejected at lowering; they are never evaluated
    // as JavaScript by the importer or render loop.
    auto expression_layer = shape_layer();
    expression_layer["exp"] = json::object();
    auto expression_result = write(root(json::array({expression_layer})).dump());
    REQUIRE_FALSE(expression_result);
    CHECK(expression_result.error().code == "lottie_unsupported_or_invalid");
    CHECK(expression_result.error().message.find("expressions") != std::string::npos);

    // Root layer count and expanded scene count have separate hard bounds.
    json too_many_layers = json::array();
    for (int i = 0; i < 257; ++i) {
        auto layer = shape_layer();
        layer["ind"] = i + 1;
        too_many_layers.push_back(std::move(layer));
    }
    expect_rejected(root(std::move(too_many_layers)).dump(), "1..256 layers");

    json too_many_expanded = json::array();
    for (int i = 0; i < 129; ++i) {
        too_many_expanded.push_back({{"ty", 0}, {"ind", i + 1},
            {"refId", "leaf"}, {"ip", 0}, {"op", 12}, {"st", 0},
            {"sr", 1}, {"ks", transform}});
    }
    expect_rejected(root(std::move(too_many_expanded), json::array({
        {{"id", "leaf"}, {"w", 64}, {"h", 64},
         {"layers", json::array({shape_layer()})}}})).dump(),
        "exceeds 256 scene layers");

    // Excessive file size is rejected before JSON parsing or allocation-heavy lowering.
    expect_rejected(std::string(10U * 1024U * 1024U + 1U, ' '),
                    "10 MiB input limit");

    // Remap NaN is represented by JSON null; malformed and negative remaps
    // must be rejected instead of producing a non-finite/out-of-range sample.
    auto malformed_remap = json{{"ty", 0}, {"ind", 1}, {"refId", "child"},
        {"ip", 0}, {"op", 12}, {"st", 0}, {"sr", 1}, {"ks", transform},
        {"tm", {{"a", 0}, {"k", nullptr}}}};
    auto child = root(json::array({shape_layer()}));
    child["layers"][0]["ind"] = 2;
    auto remap_result = write(root(json::array({malformed_remap}),
        json::array({{{"id", "child"}, {"w", 64}, {"h", 64},
                      {"layers", child["layers"]}}})).dump());
    REQUIRE_FALSE(remap_result);
    CHECK(remap_result.error().code == "lottie_unsupported_or_invalid");

    malformed_remap["tm"] = {{"a", 0}, {"k", -1.0}};
    remap_result = write(root(json::array({malformed_remap}),
        json::array({{{"id", "child"}, {"w", 64}, {"h", 64},
                      {"layers", child["layers"]}}})).dump());
    REQUIRE_FALSE(remap_result);
    CHECK(remap_result.error().message.find("[0,1000000]") != std::string::npos);

    // The recursive expansion limit is enforced independently of cycle detection.
    json chain_assets = json::array();
    for (int i = 0; i < 9; ++i) {
        json child_layer = i == 8 ? shape_layer() : json{
            {"ty", 0}, {"ind", i + 2}, {"refId", "nested_" + std::to_string(i + 1)},
            {"ip", 0}, {"op", 12}, {"st", 0}, {"sr", 1}, {"ks", transform}};
        if (i == 8) child_layer["ind"] = i + 2;
        chain_assets.push_back({{"id", "nested_" + std::to_string(i)},
            {"w", 64}, {"h", 64}, {"layers", json::array({std::move(child_layer)})}});
    }
    auto first = json{{"ty", 0}, {"ind", 1}, {"refId", "nested_0"},
        {"ip", 0}, {"op", 12}, {"st", 0}, {"sr", 1}, {"ks", transform}};
    auto depth_result = write(root(json::array({first}), std::move(chain_assets)).dump());
    REQUIRE_FALSE(depth_result);
    CHECK(depth_result.error().message.find("nesting/layer count") != std::string::npos);
}
