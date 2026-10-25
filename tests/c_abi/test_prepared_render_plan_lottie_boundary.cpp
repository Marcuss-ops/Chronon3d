#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
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

TEST_CASE("Lottie layer expansion rejects multi-layer and precomposition assets explicitly") {
    using namespace chronon3d::render_plan;
    TempDirectory directory;
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"shape","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/mark.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    for (const auto& [document, expected] : {
             std::pair{std::string{R"JSON({"v":"5.7.4","layers":[{"ty":4},{"ty":4}]})JSON"}, "multi-layer"},
             std::pair{std::string{R"JSON({"v":"5.7.4","layers":[{"ty":0,"refId":"nested"}],"assets":[{"id":"nested","layers":[]}]})JSON"}, "precompositions"}}) {
        {
            std::ofstream file(directory.path / "shapes/mark.json", std::ios::trunc);
            file << document;
        }
        const auto rejected = compile_render_plan(*decoded, resolver);
        CHECK_FALSE(rejected);
        if (!rejected) {
            CHECK(rejected.error().code == "lottie_unsupported_or_invalid");
            CHECK(rejected.error().message.find(expected) != std::string::npos);
        }
    }
}
