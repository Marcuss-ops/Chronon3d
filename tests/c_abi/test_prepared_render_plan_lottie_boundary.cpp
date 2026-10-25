#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
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
    REQUIRE(evaluated);
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
