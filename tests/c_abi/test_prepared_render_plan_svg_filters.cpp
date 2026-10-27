#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

namespace {
class TempDirectory {
public:
    TempDirectory() {
        path = std::filesystem::temp_directory_path() /
            ("chronon3d_svg_filter_plan_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path / "art");
    }
    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};
} // namespace

TEST_CASE("RenderPlan lowers bounded SVG filter chains and rejects unsupported graphs") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    TempDirectory directory;
    const auto svg_path = directory.path / "art/filter-chain.svg";
    {
        std::ofstream svg(svg_path);
        svg << R"svg(<svg filter="url(#f)"><defs><filter id="f">
          <feGaussianBlur stdDeviation="2" result="soft"/>
          <feDropShadow in="soft" dx="3" dy="-2" stdDeviation="1" flood-color="#FF0000" flood-opacity="0.5" result="shadow"/>
          <feColorMatrix in="shadow" type="matrix" values="1 0 0 0 0  0 1 0 0 0  0 0 1 0 0  0 0 0 1 0" result="graded"/>
          <feBlend in="graded" in2="BackgroundImage" mode="multiply"/>
          </filter></defs><path d="M8 8L56 8L56 56L8 56Z" fill="#ff0000"/></svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"svg","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"art/filter-chain.svg"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const std::string error_message = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message + " [" + prepared.error().code + "]";
    REQUIRE_MESSAGE(prepared.has_value(), error_message);
    const auto evaluated = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& layer = evaluated->scene.layers()[0];
    REQUIRE(layer.effects().size() == 3);
    CHECK(std::holds_alternative<BlurParams>(layer.effects()[0].params));
    const auto* shadow = std::get_if<DropShadowParams>(&layer.effects()[1].params);
    REQUIRE(shadow);
    CHECK(shadow->offset.x == doctest::Approx(3.0f));
    CHECK(shadow->offset.y == doctest::Approx(-2.0f));
    CHECK(shadow->color.a == doctest::Approx(0.5f));
    CHECK(std::holds_alternative<ColorMatrixParams>(layer.effects()[2].params));
    CHECK(layer.blend_mode == BlendMode::Multiply);

    {
        std::ofstream svg(svg_path, std::ios::trunc);
        svg << R"svg(<svg filter="url(#f)"><defs><filter id="f"><feOffset dx="1"/></filter></defs><path d="M0 0L1 1Z"/></svg>)svg";
    }
    const auto rejected = compile_render_plan(*decoded, resolver);
    CHECK_FALSE(rejected);
    if (!rejected) CHECK(rejected.error().code == "svg_import_failed");
}
