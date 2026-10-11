#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/effect_presets.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include "../../src/render_plan/render_plan_compiler_detail.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <chronon3d/render_plan/subtitle_style.hpp>
#include <chronon3d/scene/model/core/transition_catalog_ids.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/core/mask_utils.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/text/text_color_packing.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/math/expression.hpp>
#if defined(CHRONON_TEST_ENABLE_SVG)
#include <chronon3d/render_plan/render_plan.hpp>
#endif
#include <chronon3d/timeline/compile_evaluate.hpp>
#include "test_render_plan_shape_helpers.hpp"
#include "../../src/render_plan/audio_react_baker.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

class FingerprintTempDir {
public:
    FingerprintTempDir() {
        path = std::filesystem::temp_directory_path() /
               ("chronon3d_fingerprint_" +
                std::to_string(std::chrono::steady_clock::now()
                                   .time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~FingerprintTempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

chronon3d::render_plan::RenderPlan image_plan() {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "fingerprint-plan";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{3}};
    chronon3d::render_plan::LayerPlan image;
    image.id = "image";
    image.type = chronon3d::render_plan::LayerType::Image;
    image.asset = "images/source.png";
    plan.layers.push_back(std::move(image));
    return plan;
}

void write_asset(const std::filesystem::path& root, std::string contents) {
    std::filesystem::create_directories(root / "images");
    std::ofstream output(root / "images/source.png", std::ios::binary);
    output << contents;
}

std::filesystem::path repo_image_fixture() {
    auto root = std::filesystem::current_path();
    for (int depth = 0; depth < 8 && !root.empty(); ++depth) {
        const auto candidate = root / "tests/install_consumer/assets/test_image.png";
        if (std::filesystem::is_regular_file(candidate)) return candidate;
        const auto parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    throw std::runtime_error("cannot locate tests/install_consumer/assets/test_image.png");
}

} // namespace

TEST_CASE("render-plan layer transitions lower into the existing scene layer spec") {
    auto source = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"}})});
    source["layers"][0]["transition_in"] = {
        {"kind", "slide"}, {"duration_frames", 6}, {"delay_frames", 1},
        {"direction", "left"}, {"params", {{"distance", 1.25}}}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    chronon3d::assets::AssetResolver resolver;
    const auto prepared = chronon3d::render_plan::compile_render_plan(*decoded, resolver);
    if (!prepared) INFO(prepared.error().message);
    REQUIRE(prepared);

    chronon3d::FrameContextParams params;
    params.global_time = chronon3d::SampleTime::from_frame(0.0, decoded->canvas.fps);
    params.duration = decoded->canvas.duration;
    params.width = decoded->canvas.width;
    params.height = decoded->canvas.height;
    auto scene = prepared->composition->evaluate(chronon3d::make_frame_context(params));
    REQUIRE(scene.layers().size() == 1);
    CHECK(chronon3d::layer_transition_kind(
              scene.layers()[0].transition_in.transition_id) ==
          chronon3d::LayerTransitionKind::Slide);
    CHECK(scene.layers()[0].transition_in.duration == doctest::Approx(0.2));
    CHECK(scene.layers()[0].transition_in.delay == doctest::Approx(1.0 / 30.0));
    const auto transition_hash = chronon3d::graph::hash_layer_transition_spec(
        scene.layers()[0].transition_in);
    auto changed_spec = scene.layers()[0].transition_in;
    changed_spec.parameters = chronon3d::SlideParams{1.5f};
    CHECK(chronon3d::graph::hash_layer_transition_spec(changed_spec) != transition_hash);
}

// ── media card / video corner contract ─────────────────────────────
//
// A media layer may declare the rounded plate behind it (style.background) and
// the corner radius of its own box (radius). One compile must produce:
//
//   - a colour plate layer materialized BEFORE the clip, sized to the clip's
//     displayed box plus 2*padding, carrying the plate colour and radius and
//     the drop shadow (the plate IS the frame);
//   - the clip itself unchanged in size, with its rounded-rect mask derived
//     from radius and no residual shadow/background declaration.
TEST_CASE("media background card and video radius lower into plate plus corner mask") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "video");
    {
        std::ofstream file(directory.path / "video/source.mp4", std::ios::binary);
        file << "video bytes";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v2","version":2,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":2},
      "layers":[{"id":"source","type":"video","source":"video/source.mp4","size":[32,32],
        "radius":4,
        "style":{
          "background":{"color":"#FFFFFF","radius":6,"padding":[4,4]},
          "shadow":{"color":"#000000","opacity":0.5,"blur":4,"offset":[0,2]}}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_message = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_message);

    FrameContextParams params;
    params.global_time = SampleTime::from_frame(0.0, decoded->canvas.fps);
    params.duration = decoded->canvas.duration;
    params.width = decoded->canvas.width;
    params.height = decoded->canvas.height;
    const auto scene = prepared->composition->evaluate(make_frame_context(params));
    REQUIRE(scene.layers().size() == 2);
    // z-order: the plate is composited UNDER the clip.
    CHECK(scene.layers()[0].name == "source__frame");
    CHECK(scene.layers()[1].name == "source");

    const auto& plate = scene.layers()[0];
    REQUIRE(!plate.nodes.empty());
    CHECK(plate.nodes.front().corner_radius == doctest::Approx(6.0f));
    CHECK(plate.transform.position.x == doctest::Approx(32.0f));
    CHECK(plate.transform.position.y == doctest::Approx(32.0f));
    CHECK(plate.nodes.front().color.r == doctest::Approx(1.0f));
    REQUIRE(!plate.effects().empty());
    CHECK(plate.effects()[0].enabled);

    const auto& clip = scene.layers()[1];
    REQUIRE(clip.video_source != nullptr);
    CHECK(clip.video_source->size.x == doctest::Approx(32.0f));
    CHECK(clip.mask.enabled());
    CHECK(clip.mask.type == MaskType::RoundedRect);
    CHECK(clip.mask.radius == doctest::Approx(4.0f));
    CHECK(clip.mask.size.x == doctest::Approx(32.0f));
    CHECK(clip.mask.feather == doctest::Approx(1.0f));
    CHECK(clip.transform.position.x == doctest::Approx(0.0f));
    CHECK(clip.transform.position.y == doctest::Approx(0.0f));
    CHECK(clip.effects().empty());
}

TEST_CASE("media background card and rounded stroke frame an image") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::ifstream fixture(repo_image_fixture(), std::ios::binary);
    REQUIRE(fixture.good());
    const std::string image_bytes{std::istreambuf_iterator<char>(fixture),
                                  std::istreambuf_iterator<char>()};
    write_asset(directory.path, image_bytes);

    auto plan = image_plan();
    plan.layers.front().size = {32.0f, 32.0f};
    plan.layers.front().size_dimensions = 2;
    plan.layers.front().position = {32.0f, 32.0f, 0.0f};
    plan.layers.front().position_dimensions = 3;
    plan.layers.front().opacity = 0.0f;
    plan.layers.front().radius = 4.0f;
    AnimationPlan media_animation;
    AnimationTrackPlan opacity_track;
    opacity_track.property = "opacity";
    opacity_track.keyframes = {{Frame{0}, {0.0f}}, {Frame{2}, {1.0f}}};
    media_animation.tracks.push_back(opacity_track);
    AnimationTrackPlan scale_track;
    scale_track.property = "scale";
    scale_track.keyframes = {{Frame{0}, {1.0f, 1.0f, 1.0f}},
                             {Frame{2}, {1.5f, 1.5f, 1.0f}}};
    media_animation.tracks.push_back(scale_track);
    plan.layers.front().animation = std::move(media_animation);
    LayerStylePlan style;
    style.background = BackgroundStyle{
        "#FFFFFF", 1.0f, 6.0f, {4.0f, 4.0f}, 2};
    plan.layers.front().style = style;

    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto prepared = compile_render_plan(plan, resolver);
    if (!prepared) INFO(prepared.error().message);
    REQUIRE(prepared);

    FrameContextParams params;
    params.global_time = SampleTime::from_frame(0.0, plan.canvas.fps);
    params.duration = plan.canvas.duration;
    params.width = plan.canvas.width;
    params.height = plan.canvas.height;
    const auto scene = prepared->composition->evaluate(make_frame_context(params));
    REQUIRE(scene.layers().size() == 2);
    CHECK(scene.layers()[0].name == "image__frame");
    CHECK(scene.layers()[1].name == "image");
    REQUIRE(!scene.layers()[0].nodes.empty());
    const auto& frame_node = scene.layers()[0].nodes.front();
    REQUIRE(frame_node.shape.type() == ShapeType::RoundedRect);
    CHECK(frame_node.shape.rounded_rect().size.x == doctest::Approx(40.0f));
    CHECK(frame_node.shape.rounded_rect().size.y == doctest::Approx(40.0f));
    CHECK(frame_node.shape.rounded_rect().radius == doctest::Approx(6.0f));
    CHECK(frame_node.corner_radius == doctest::Approx(6.0f));
    CHECK(scene.layers()[0].transform.position.x ==
          doctest::Approx(scene.layers()[1].transform.position.x));
    CHECK(scene.layers()[0].transform.position.y ==
          doctest::Approx(scene.layers()[1].transform.position.y));
    CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.0f));
    REQUIRE(scene.layers()[0].anim_transform.scale.is_animated());
    CHECK(scene.layers()[0].anim_transform.scale.evaluate(SampleTime::from_frame(0.0, plan.canvas.fps)).x ==
          doctest::Approx(1.0f));
    CHECK(scene.layers()[0].anim_transform.scale.evaluate(SampleTime::from_frame(2.0, plan.canvas.fps)).x ==
          doctest::Approx(1.5f));
    REQUIRE(scene.layers()[1].nodes.size() == 1);
    CHECK(scene.layers()[1].nodes.front().shape.type() == ShapeType::Image);
    CHECK(scene.layers()[1].nodes.front().shape.image().radius == doctest::Approx(4.0f));
    params.global_time = SampleTime::from_frame(2.0, plan.canvas.fps);
    const auto final_scene = prepared->composition->evaluate(make_frame_context(params));
    CHECK(final_scene.layers()[0].transform.opacity == doctest::Approx(1.0f));
    CHECK(final_scene.layers()[1].transform.opacity == doctest::Approx(1.0f));
    CHECK(final_scene.layers()[0].transform.scale.x == doctest::Approx(1.5f));
}

// A shadow without a plate is the clip's own shadow (the widened style.shadow
// gate): no plate is materialized and the clip keeps its square corners.
TEST_CASE("video shadow without a background stays on the clip") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "video");
    {
        std::ofstream file(directory.path / "video/source.mp4", std::ios::binary);
        file << "video bytes";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v2","version":2,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"source","type":"video","source":"video/source.mp4","size":[32,32],
        "style":{"shadow":{"color":"#000000","opacity":0.4,"blur":3,"offset":[1,2]}}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_message = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_message);

    FrameContextParams params;
    params.global_time = SampleTime::from_frame(0.0, decoded->canvas.fps);
    params.duration = decoded->canvas.duration;
    params.width = decoded->canvas.width;
    params.height = decoded->canvas.height;
    const auto scene = prepared->composition->evaluate(make_frame_context(params));
    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].name == "source");
    CHECK(!scene.layers()[0].mask.enabled());
    REQUIRE(!scene.layers()[0].effects().empty());
    CHECK(scene.layers()[0].effects()[0].enabled);
}

TEST_CASE("render plan lowers a bounded static Lottie path through the resolver") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/mark.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[8,10,0]},
            "s":{"a":0,"k":[120,80,100]},"r":{"a":0,"k":15},"o":{"a":0,"k":75}},
          "shapes":[{"ty":"gr","it":[{"ty":"gr","it":[{"ty":"sh","ks":{"a":0,"k":{"c":true,
            "v":[[0,0],[20,0],[20,20]],"i":[[0,0],[0,0],[0,0]],
            "o":[[0,0],[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}},
            {"ty":"st","r":1,"c":{"a":0,"k":[1,1,1,1]},"w":{"a":0,"k":1},
              "d":[{"n":"d","v":{"a":0,"k":4}},
                {"n":"g","v":{"a":0,"k":2}},
                {"n":"o","v":{"a":0,"k":1}}]},
            {"ty":"tr","a":{"a":0,"k":[0,0]},"p":{"a":0,"k":[10,0]},
              "s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":50}}]},
            {"ty":"tr","a":{"a":0,"k":[0,0]},"p":{"a":0,"k":[5,0]},
              "s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}}]},
            {"ty":"sh","ks":{"a":0,"k":{"c":true,
            "v":[[30,0],[50,0],[50,20]],"i":[[0,0],[0,0],[0,0]],
            "o":[[0,0],[0,0],[0,0]]}}},
            {"ty":"el","p":{"a":0,"k":[0,30]},"s":{"a":0,"k":[20,10]}},
            {"ty":"rc","p":{"a":0,"k":[30,30]},"s":{"a":0,"k":[20,10]},"r":{"a":0,"k":2}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[0,1,0,1]}},
            {"ty":"gf","r":1,"t":1,"s":{"a":0,"k":[0,30]},
              "e":{"a":0,"k":[20,30]},"o":{"a":0,"k":100},
              "g":{"p":2,"k":{"a":0,"k":[0,0,0,1,1,1,1,1]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,1,1,1]}},
            {"ty":"st","r":1,"c":{"a":0,"k":[1,1,1,1]},"w":{"a":0,"k":2}},
            {"ty":"st","r":1,"c":{"a":0,"k":[1,1,1,1]},"w":{"a":0,"k":3}},
            {"ty":"st","r":1,"c":{"a":0,"k":[1,1,1,1]},"w":{"a":0,"k":4}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/mark.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_message = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_message);
    CHECK(prepared->assets.assets().size() == 1);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    CHECK(evaluated->scene.layers()[0].transform.position.x == doctest::Approx(-24.0f));
    CHECK(evaluated->scene.layers()[0].transform.position.y == doctest::Approx(-22.0f));
    CHECK(evaluated->scene.layers()[0].transform.scale.x == doctest::Approx(1.2f));
    CHECK(evaluated->scene.layers()[0].transform.scale.y == doctest::Approx(0.8f));
    CHECK(glm::degrees(glm::eulerAngles(evaluated->scene.layers()[0].transform.rotation).z) ==
          doctest::Approx(15.0f));
    CHECK(evaluated->scene.layers()[0].transform.opacity == doctest::Approx(0.75f));
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 4);
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().commands.size() == 4);
    CHECK(evaluated->scene.layers()[0].nodes[1].shape.path().commands.size() == 4);
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().commands.size() == 6);
    CHECK(evaluated->scene.layers()[0].nodes[3].shape.path().commands.size() == 10);
    const auto& lottie_nodes = evaluated->scene.layers()[0].nodes;
    CHECK(lottie_nodes[0].shape.path().fill.solid.r == doctest::Approx(1.0f));
    CHECK(lottie_nodes[0].shape.path().fill.solid.a == doctest::Approx(0.5f));
    CHECK(lottie_nodes[0].shape.path().commands[0].p0.x == doctest::Approx(15.0f));
    CHECK(lottie_nodes[0].shape.path().fill.solid.g == doctest::Approx(0.0f));
    CHECK(lottie_nodes[1].shape.path().fill.solid.r == doctest::Approx(0.0f));
    CHECK(lottie_nodes[1].shape.path().fill.solid.g == doctest::Approx(1.0f));
    CHECK(lottie_nodes[2].shape.path().fill.gradient.type == graphics::GradientType::Linear);
    REQUIRE(lottie_nodes[2].shape.path().fill.gradient.color_stops.size() == 2);
    CHECK(lottie_nodes[2].shape.path().fill.gradient.color_stops[0].color.b == doctest::Approx(1.0f));
    CHECK(lottie_nodes[3].shape.path().fill.solid.r == doctest::Approx(1.0f));
    CHECK(lottie_nodes[0].shape.path().stroke.enabled);
    CHECK(lottie_nodes[0].shape.path().stroke.width == doctest::Approx(1.0f));
    REQUIRE(lottie_nodes[0].shape.path().stroke.dash_array.size() == 2);
    CHECK(lottie_nodes[0].shape.path().stroke.dash_array[0] == doctest::Approx(4.0f));
    CHECK(lottie_nodes[0].shape.path().stroke.dash_offset == doctest::Approx(1.0f));
    CHECK(lottie_nodes[3].shape.path().stroke.width == doctest::Approx(4.0f));

    auto unsupported = *decoded;
    // The decoder is allowed to retain the reference; the compiler rejects
    // unsupported expressions explicitly instead of silently dropping them.
    {
        std::ofstream file(directory.path / "shapes/mark.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,"exp":"x"}]})JSON";
    }
    CHECK_FALSE(compile_render_plan(unsupported, resolver));

    using json = nlohmann::json;
    const auto identity_layer = [] {
        return json{{"ty", 4}, {"ddd", 0},
            {"ks", {{"a", {{"a", 0}, {"k", {0, 0, 0}}}},
                    {"p", {{"a", 0}, {"k", {0, 0, 0}}}},
                    {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
                    {"r", {{"a", 0}, {"k", 0}}},
                    {"o", {{"a", 0}, {"k", 100}}}}},
            {"shapes", json::array()}};
    };
    const auto static_path = [] {
        return json{{"ty", "sh"}, {"ks", {{"a", 0}, {"k", {
            {"c", true}, {"v", {{0, 0}, {10, 0}}},
            {"i", {{0, 0}, {0, 0}}}, {"o", {{0, 0}, {0, 0}}}}}}}};
    };
    std::vector<json> unsupported_documents;
    {
        auto layer = identity_layer();
        layer["masksProperties"] = json::array({json{{"mode", "a"}}});
        layer["shapes"].push_back(static_path());
        layer["shapes"].push_back(json{{"ty", "fl"}, {"r", 1},
            {"c", {{"a", 0}, {"k", {1, 0, 0, 1}}}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({json{{"ty", "gr"}, {"it", json::array()}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        const auto animated_group_transform = json{{"ty", "tr"},
            {"a", {{"a", 0}, {"k", {0, 0}}}},
            {"p", {{"a", 1}, {"k", {{{"t", 0}, {"s", {0, 0}}}, {{"t", 10}, {"s", {10, 0}}}}}}},
            {"s", {{"a", 0}, {"k", {100, 100}}}},
            {"r", {{"a", 0}, {"k", 0}}}, {"o", {{"a", 0}, {"k", 100}}}};
        const auto static_group_transform = json{{"ty", "tr"},
            {"a", {{"a", 0}, {"k", {0, 0}}}}, {"p", {{"a", 0}, {"k", {5, 0}}}},
            {"s", {{"a", 0}, {"k", {100, 100}}}},
            {"r", {{"a", 0}, {"k", 0}}}, {"o", {{"a", 0}, {"k", 100}}}};
        layer["shapes"] = json::array({json{{"ty", "gr"}, {"it", json::array({
            json{{"ty", "gr"}, {"it", json::array({static_path(), animated_group_transform})}},
            static_group_transform})}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({json{{"ty", "el"},
            {"p", {{"a", 1}, {"k", json::array()}}},
            {"s", {{"a", 0}, {"k", {20, 10}}}},
            {"c", {{"a", 0}, {"k", {1, 0, 0, 1}}}}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({static_path(),
            json{{"ty", "gf"}, {"g", {{"p", 2}, {"k", {{"a", 0}, {"k", json::array()}}}}}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({static_path(), static_path(),
            json{{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {1, 0, 0, 1}}}}},
            json{{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {0, 1, 0, 1}}}}},
            json{{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {0, 0, 1, 1}}}}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({static_path(),
            json{{"ty", "st"}, {"r", 1}, {"c", {{"a", 0}, {"k", {1, 1, 1, 1}}}},
                {"w", {{"a", 0}, {"k", 2}}},
                {"d", json::array({json{{"n", "unsupported"}, {"v", {{"a", 0}, {"k", 4}}}}})}}});
        unsupported_documents.push_back(json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    for (const auto& document : unsupported_documents) {
        { std::ofstream file(directory.path / "shapes/mark.json"); file << document.dump(); }
        CHECK_FALSE(compile_render_plan(unsupported, resolver));
    }
}

TEST_CASE("render plan rejects out-of-contract Lottie inputs at every limit boundary") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    using json = nlohmann::json;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/edge.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto identity_layer = [] {
        return json{{"ty", 4}, {"ddd", 0},
            {"ks", {{"a", {{"a", 0}, {"k", {0, 0, 0}}}},
                    {"p", {{"a", 0}, {"k", {0, 0, 0}}}},
                    {"s", {{"a", 0}, {"k", {100, 100, 100}}}},
                    {"r", {{"a", 0}, {"k", 0}}},
                    {"o", {{"a", 0}, {"k", 100}}}}},
            {"shapes", json::array()}};
    };
    const auto static_path = [] {
        return json{{"ty", "sh"}, {"ks", {{"a", 0}, {"k", {
            {"c", true}, {"v", {{0, 0}, {10, 0}}},
            {"i", {{0, 0}, {0, 0}}}, {"o", {{0, 0}, {0, 0}}}}}}}};
    };
    const auto solid_fill = [] {
        return json{{"ty", "fl"}, {"r", 1}, {"c", {{"a", 0}, {"k", {1, 0, 0, 1}}}}};
    };
    std::vector<std::pair<std::string, json>> edge_documents;
    // 1. Group nesting deeper than 16 levels fails closed.
    {
        json nested = json{{"ty", "gr"}, {"it", json::array({static_path(), solid_fill()})}};
        for (int level = 0; level < 17; ++level)
            nested = json{{"ty", "gr"}, {"it", json::array({nested})}};
        auto layer = identity_layer();
        layer["shapes"].push_back(nested);
        edge_documents.emplace_back("deep nesting (17 levels)",
            json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    // 2. More than 256 authored shape items fails closed.
    {
        auto layer = identity_layer();
        for (int i = 0; i < 257; ++i) layer["shapes"].push_back(static_path());
        edge_documents.emplace_back("257 shape items",
            json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    // 3. Coordinates beyond +/-100000 fail closed.
    {
        auto layer = identity_layer();
        auto far = static_path();
        far["ks"]["k"]["v"] = json::array({{200000, 0}, {200010, 0}});
        layer["shapes"] = json::array({far, solid_fill()});
        edge_documents.emplace_back("out-of-bounds coordinate",
            json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    // 4. Non-finite coordinates (1e999 parses to inf) fail closed.
    {
        auto document = json::parse(
            R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,
              "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
                "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
              "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":true,
                "v":[[1e999,0],[10,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}}},
                {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON");
        edge_documents.emplace_back("non-finite coordinate", document);
    }
    // 5. Non-shape layer kinds fail closed (text, image, solid).
    for (const int layer_type : {1, 2, 5}) {
        auto layer = identity_layer();
        layer["ty"] = layer_type;
        layer["shapes"].push_back(static_path());
        edge_documents.emplace_back("non-shape layer ty=" + std::to_string(layer_type),
            json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    // 6. Multiple layers fail closed (contract allows exactly one).
    {
        auto layer = identity_layer();
        layer["shapes"].push_back(static_path());
        edge_documents.emplace_back("two layers",
            json{{"v", "5.7.4"}, {"layers", {layer, layer}}});
    }
    // 7. Repeater copies above 256 fail closed.
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({static_path(), solid_fill(),
            json{{"ty", "rp"}, {"c", {{"a", 0}, {"k", 300}}}},
            json{{"ty", "tr"}, {"a", {{"a", 0}, {"k", {0, 0}}}},
                {"p", {{"a", 0}, {"k", {0, 0}}}},
                {"s", {{"a", 0}, {"k", {100, 100}}}},
                {"r", {{"a", 0}, {"k", 0}}}}});
        edge_documents.emplace_back("repeater with 300 copies",
            json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    // 8. L4 morph with a single keyframe fails closed (needs 2-256).
    {
        auto layer = identity_layer();
        layer["shapes"] = json::array({
            json{{"ty", "sh"}, {"ks", {{"a", 1}, {"k", json::array({
                json{{"t", 0}, {"s", json::array({
                    json{{"c", true}, {"v", {{0, 0}, {10, 0}}},
                        {"i", {{0, 0}, {0, 0}}}, {"o", {{0, 0}, {0, 0}}}}}})}}}}}}}}});
        edge_documents.emplace_back("single-keyframe morph",
            json{{"v", "5.7.4"}, {"layers", {layer}}});
    }
    for (const auto& [label, document] : edge_documents) {
        { std::ofstream file(directory.path / "shapes/edge.json"); file << document.dump(); }
        CHECK_FALSE_MESSAGE(compile_render_plan(*decoded, resolver), label);
    }
    // 9. Input larger than 10 MiB fails closed before parsing.
    {
        std::ofstream file(directory.path / "shapes/edge.json", std::ios::binary);
        const std::string block(1024 * 1024, ' ');
        for (int i = 0; i < 11; ++i) file << block;
    }
    CHECK_FALSE(compile_render_plan(*decoded, resolver));
}

TEST_CASE("render plan lowers animated Lottie group transforms through native tracks") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/group.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"gr","it":[
            {"ty":"sh","ks":{"a":0,"k":{"c":false,"v":[[0,0],[10,0]],
              "i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}},
            {"ty":"st","r":1,"c":{"a":0,"k":[0,0,1,1]},"w":{"a":0,"k":2}},
            {"ty":"tr","a":{"a":0,"k":[0,0]},
              "p":{"a":1,"k":[{"t":0,"s":[0,0],"e":[0,0]},{"t":10,"s":[10,0]}]},
              "s":{"a":1,"k":[{"t":0,"s":[100,100],"e":[100,100]},{"t":10,"s":[200,200]}]},
              "r":{"a":1,"k":[{"t":0,"s":[0],"e":[0]},{"t":10,"s":[90]}]},
              "o":{"a":1,"k":[{"t":0,"s":[100],"e":[100]},{"t":10,"s":[50]}]}}
          ]}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"group","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/group.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    for (const auto [frame, expected_x, expected_y, expected_alpha, expected_width] : {
            std::tuple{0, 10.0f, 0.0f, 1.0f, 2.0f},
            std::tuple{5, 15.6066f, 10.6066f, 0.75f, 3.0f},
            std::tuple{10, 10.0f, 20.0f, 0.5f, 4.0f}}) {
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{frame});
        REQUIRE(evaluated);
        const auto& node = evaluated->scene.layers()[0].nodes[0];
        CHECK(node.shape.path().commands[1].p0.x == doctest::Approx(expected_x));
        CHECK(node.shape.path().commands[1].p0.y == doctest::Approx(expected_y));
        CHECK(node.fill.solid.a == doctest::Approx(expected_alpha));
        CHECK(node.shape.path().stroke.width == doctest::Approx(expected_width));
    }
}

TEST_CASE("render plan lowers Lottie masks with animated opacity and path") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/mask.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,
          "layers":[{"ty":4,"ddd":0,
            "masksProperties":[{"mode":"s","inv":false,
            "pt":{"a":0,"k":{"c":true,"v":[[0,0],[20,0],[20,20]],
              "i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}},
            "o":{"a":1,"k":[{"t":0,"s":[100],"e":[100]},{"t":10,"s":[50]}]},
            "x":{"a":0,"k":2},"f":{"a":0,"k":[2,2]}},
            {"mode":"a","inv":false,
             "pt":{"a":1,"k":[
               {"t":0,"s":[{"c":true,"v":[[0,0],[20,0],[20,20]],
                 "i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}],
                "e":[{"c":true,"v":[[0,0],[30,0],[30,30]],
                 "i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}]},
               {"t":10,"s":[{"c":true,"v":[[0,0],[30,0],[30,30]],
                 "i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}]}]},
             "o":{"a":0,"k":100}}],
            "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
              "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
            "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":true,
              "v":[[0,0],[30,0],[30,30]],"i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}}},
              {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"masked","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/mask.json"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    for (const auto [frame, expected_opacity] : {
            std::pair{0, 1.0f}, std::pair{5, 0.75f}, std::pair{10, 0.5f}}) {
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{frame});
        REQUIRE(evaluated);
        const auto& mask = evaluated->scene.layers()[0].mask;
        CHECK(mask.type == MaskType::Path);
        CHECK(mask.initial_mode == MaskCombineMode::Subtract);
        CHECK(mask.opacity == doctest::Approx(expected_opacity));
        CHECK(mask.feather == doctest::Approx(2.0f));
        CHECK(mask.expansion == doctest::Approx(2.0f));
        CHECK(mask.path.size() == 4);
        REQUIRE(mask.operations.size() == 1);
        CHECK(mask.operations[0].path[1].p0.x == doctest::Approx(
            frame == 0 ? 10.0f : frame == 5 ? 15.0f : 20.0f));
    }
    nlohmann::json incompatible;
    {
        std::ifstream input(directory.path / "shapes/mask.json");
        input >> incompatible;
    }
    auto& target_shape = incompatible["layers"][0]["masksProperties"][1]
        ["pt"]["k"][0]["e"][0];
    target_shape["v"].push_back({40, 40});
    target_shape["i"].push_back({0, 0});
    target_shape["o"].push_back({0, 0});
    {
        std::ofstream output(directory.path / "shapes/mask.json");
        output << incompatible.dump();
    }
    CHECK_FALSE(compile_render_plan(*decoded, resolver));
}

TEST_CASE("render plan lowers bounded two-keyframe Lottie L4 path morphs") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/morph.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":1,"k":[
            {"t":0,"s":[{"c":true,"v":[[0,0],[10,0],[10,10]],"i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}],
              "e":[{"c":true,"v":[[0,0],[20,0],[20,20]],"i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}]},
            {"t":10,"s":[{"c":true,"v":[[0,0],[20,0],[20,20]],"i":[[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0]]}]}]}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"morph","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/morph.json"}}],"output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    const auto first = evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    const auto last = evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{10});
    REQUIRE(first);
    REQUIRE(last);
    const auto& first_path = first->scene.layers()[0].nodes[0].shape.path().commands;
    const auto& last_path = last->scene.layers()[0].nodes[0].shape.path().commands;
    REQUIRE(first_path.size() == last_path.size());
    CHECK(first_path[1].p0.x == doctest::Approx(10.0f));
    CHECK(last_path[1].p0.x == doctest::Approx(20.0f));
}

TEST_CASE("render plan lowers multi-keyframe Lottie path morphs piecewise") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/morph.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":21,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":1,"k":[
            {"t":0,"s":[{"c":false,"v":[[0,0],[10,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}],
              "e":[{"c":false,"v":[[0,0],[20,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}]},
            {"t":10,"s":[{"c":false,"v":[[0,0],[20,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}],
              "e":[{"c":false,"v":[[0,0],[30,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}]},
            {"t":20,"s":[{"c":false,"v":[[0,0],[30,0]],"i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}]}]}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":21},
      "layers":[{"id":"morph","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/morph.json"}}],"output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    for (const auto [frame, expected_x] : {std::pair{5, 15.0f}, std::pair{10, 20.0f},
                                            std::pair{15, 25.0f}, std::pair{20, 30.0f}}) {
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{frame});
        REQUIRE(evaluated);
        const auto& commands = evaluated->scene.layers()[0].nodes[0].shape.path().commands;
        REQUIRE(commands.size() == 2);
        CHECK(commands[1].p0.x == doctest::Approx(expected_x));
    }
}

TEST_CASE("render plan lowers animated Lottie Trim Paths into the native trim track") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    const std::string lottie_source = R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,"layers":[{"ty":4,"ddd":0,
      "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
        "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
      "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":false,"v":[[0,0],[100,0]],
        "i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}}},
        {"ty":"tm","s":{"a":0,"k":0},"e":{"a":1,"k":[
          {"t":0,"s":[50],"o":{"x":0.333,"y":0.333}},
          {"t":10,"s":[50],"i":{"x":0.667,"y":0.667}}]},
          "o":{"a":1,"k":[{"t":0,"s":[0],"o":{"x":0.333,"y":0.333}},
            {"t":10,"s":[180],"i":{"x":0.667,"y":0.667}}]},"m":1},
        {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
    const auto write_source = [&](const std::string& source) {
        std::ofstream file(directory.path / "shapes/trim.json");
        file << source;
    };
    write_source(lottie_source);
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":128,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"trim","type":"shape","size":[128,64],
        "shape":{"type":"path","lottie":"shapes/trim.json"}}],
      "output":{"path":"out.mp4"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    const auto first = evaluate(prepared->compiled_composition,
                                CompositionEvaluateContext{}, Frame{0});
    const auto last = evaluate(prepared->compiled_composition,
                               CompositionEvaluateContext{}, Frame{10});
    const auto middle = evaluate(prepared->compiled_composition,
                                 CompositionEvaluateContext{}, Frame{5});
    REQUIRE(first);
    REQUIRE(last);
    REQUIRE(middle);
    const auto& first_path = first->scene.layers()[0].nodes[0].shape.path().commands;
    const auto& last_path = last->scene.layers()[0].nodes[0].shape.path().commands;
    const auto& middle_path = middle->scene.layers()[0].nodes[0].shape.path().commands;
    REQUIRE(first_path.size() >= 2);
    REQUIRE(last_path.size() >= 2);
    REQUIRE(middle_path.size() >= 2);
    CHECK(first_path.front().p0.x == doctest::Approx(0.0f));
    CHECK(first_path.back().p0.x == doctest::Approx(50.0f));
    CHECK(middle_path.front().p0.x == doctest::Approx(25.0f));
    CHECK(middle_path.back().p0.x == doctest::Approx(75.0f));
    CHECK(last_path.front().p0.x == doctest::Approx(50.0f));
    CHECK(last_path.back().p0.x == doctest::Approx(100.0f));

    auto unsupported_mode = lottie_source;
    const auto mode = unsupported_mode.find("\"m\":1");
    REQUIRE(mode != std::string::npos);
    unsupported_mode.replace(mode, std::string("\"m\":1").size(), "\"m\":2");
    write_source(unsupported_mode);
    CHECK_FALSE(compile_render_plan(*decoded, resolver));

    auto mismatched_timing = lottie_source;
    const auto last_offset_frame = mismatched_timing.find("\"t\":10,\"s\":[180]");
    REQUIRE(last_offset_frame != std::string::npos);
    mismatched_timing.replace(last_offset_frame, std::string("\"t\":10,\"s\":[180]").size(),
                              "\"t\":11,\"s\":[180]");
    write_source(mismatched_timing);
    CHECK_FALSE(compile_render_plan(*decoded, resolver));
}

TEST_CASE("render plan lowers animated Lottie solid fill and stroke paint") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/paint.json");
        file << R"JSON({"v":"5.7.4","ip":0,"op":11,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":false,"v":[[0,0],[100,0]],
            "i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":1,"k":[
              {"t":0,"s":[1,0,0,1],"o":{"x":0.333,"y":0.333}},
              {"t":10,"s":[0,0,1,1],"i":{"x":0.667,"y":0.667}}]},
              "o":{"a":1,"k":[{"t":0,"s":[100],"o":{"x":0.333,"y":0.333}},
                {"t":10,"s":[50],"i":{"x":0.667,"y":0.667}}]}},
            {"ty":"st","r":1,"c":{"a":0,"k":[0,1,0,1]},"w":{"a":1,"k":[
              {"t":0,"s":[2],"o":{"x":0.333,"y":0.333}},
              {"t":10,"s":[10],"i":{"x":0.667,"y":0.667}}]}}]}]})JSON";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":128,"height":64,"fps_num":30,"fps_den":1,"duration_frames":11},
      "layers":[{"id":"paint","type":"shape","size":[128,64],
        "shape":{"type":"path","lottie":"shapes/paint.json"}}],
      "output":{"path":"out.mp4"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    const auto at_start = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    const auto at_middle = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{5});
    const auto at_end = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{10});
    REQUIRE(at_start);
    REQUIRE(at_middle);
    REQUIRE(at_end);
    const auto& start = at_start->scene.layers()[0].nodes[0].shape.path();
    const auto& middle = at_middle->scene.layers()[0].nodes[0].shape.path();
    const auto& end = at_end->scene.layers()[0].nodes[0].shape.path();
    CHECK(start.fill.solid.r == doctest::Approx(1.0f));
    CHECK(start.fill.solid.b == doctest::Approx(0.0f));
    CHECK(start.fill.solid.a == doctest::Approx(1.0f));
    CHECK(middle.fill.solid.r == doctest::Approx(0.5f));
    CHECK(middle.fill.solid.b == doctest::Approx(0.5f));
    CHECK(middle.fill.solid.a == doctest::Approx(0.75f));
    CHECK(end.fill.solid.r == doctest::Approx(0.0f));
    CHECK(end.fill.solid.b == doctest::Approx(1.0f));
    CHECK(end.fill.solid.a == doctest::Approx(0.5f));
    CHECK(start.stroke.width == doctest::Approx(2.0f));
    CHECK(middle.stroke.width == doctest::Approx(6.0f));
    CHECK(end.stroke.width == doctest::Approx(10.0f));
}

TEST_CASE("render plan lowers animated Lottie layer transforms to native tracks") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/motion.json");
        const std::string lottie_source = R"JSON({"v":"5.7.4","ip":0,"op":12,"fr":30,"layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[5,0,0]},
            "p":{"a":1,"k":[{"t":0,"s":[10,20,0],"o":{"x":0.333,"y":0.333}},
              {"t":10,"s":[50,20,0],"i":{"x":0.667,"y":0.667}}]},
            "s":{"a":1,"k":[{"t":0,"s":[100,100,100]},
              {"t":10,"s":[200,200,100]}]},
            "r":{"a":1,"k":[{"t":0,"s":[0],"h":1},{"t":10,"s":[90]}]},
            "o":{"a":1,"k":[{"t":0,"s":[100]},{"t":10,"s":[0]}]}},
          "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":true,"v":[[0,0],[10,0]],
            "i":[[0,0],[0,0]],"o":[[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
        file << lottie_source;
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"motion","type":"shape","size":[64,64],
        "shape":{"type":"path","lottie":"shapes/motion.json"}}],
      "output":{"path":"out.mp4"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto start = evaluate(prepared->compiled_composition,
                                CompositionEvaluateContext{}, Frame{0});
    const auto end = evaluate(prepared->compiled_composition,
                              CompositionEvaluateContext{}, Frame{10});
    const auto middle = evaluate(prepared->compiled_composition,
                                 CompositionEvaluateContext{}, Frame{5});
    REQUIRE_MESSAGE(start.has_value(), start.error().message);
    REQUIRE_MESSAGE(end.has_value(), end.error().message);
    REQUIRE_MESSAGE(middle.has_value(), middle.error().message);
    REQUIRE(start->scene.layers().size() == 1);
    REQUIRE(end->scene.layers().size() == 1);
    CHECK(end->scene.layers()[0].transform.position.x -
          start->scene.layers()[0].transform.position.x == doctest::Approx(40.0f));
    CHECK(middle->scene.layers()[0].transform.scale.x == doctest::Approx(1.5f));
    CHECK(middle->scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
    CHECK(glm::degrees(glm::eulerAngles(middle->scene.layers()[0].transform.rotation).z) ==
          doctest::Approx(0.0f));
    CHECK(glm::degrees(glm::eulerAngles(end->scene.layers()[0].transform.rotation).z) ==
          doctest::Approx(90.0f));
    CHECK(start->scene.layers()[0].nodes[0].shape.path().fill.solid.r == doctest::Approx(1.0f));
    CHECK(start->scene.layers()[0].nodes[0].shape.path().commands[0].p0.x ==
          doctest::Approx(-5.0f));

    const auto original_source = [&] {
        std::ifstream input(directory.path / "shapes/motion.json");
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    }();
    auto changed_source = original_source;
    const auto changed_value = changed_source.find("[50,20,0]");
    REQUIRE(changed_value != std::string::npos);
    changed_source.replace(changed_value, std::string("[50,20,0]").size(), "[51,20,0]");
    {
        std::ofstream file(directory.path / "shapes/motion.json");
        file << changed_source;
    }
    const auto changed = compile_render_plan(*decoded, resolver);
    REQUIRE(changed);
    CHECK(changed->fingerprint.content_digest != prepared->fingerprint.content_digest);

    {
        std::ofstream file(directory.path / "shapes/repeater.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0]},"p":{"a":0,"k":[0,0]},
            "s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":true,
            "v":[[0,0],[10,0],[10,10]],"i":[[0,0],[0,0],[0,0]],
            "o":[[0,0],[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}},
            {"ty":"rp","c":{"a":0,"k":3},"tr":{"a":{"a":0,"k":[0,0]},
              "p":{"a":0,"k":[10,0]},"s":{"a":0,"k":[100,100]},
              "r":{"a":0,"k":0},"so":{"a":0,"k":100},"eo":{"a":0,"k":50}}}]}]})JSON";
    }
    auto repeater_plan = *decoded;
    repeater_plan.layers[0].shape->lottie_asset = "shapes/repeater.json";
    const auto repeater_prepared = compile_render_plan(repeater_plan, resolver);
    REQUIRE(repeater_prepared);
    const auto repeated = evaluate(repeater_prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    REQUIRE(repeated);
    REQUIRE(repeated->scene.layers().size() == 1);
    REQUIRE(repeated->scene.layers()[0].nodes.size() == 3);
    CHECK(repeated->scene.layers()[0].nodes[2].fill.solid.a == doctest::Approx(0.5f));

    {
        std::ofstream file(directory.path / "shapes/motion.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":1,"k":[{"t":0,"s":[0,0]},
            {"t":10,"s":[5,0]}]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},
            "o":{"a":0,"k":100}},"shapes":[]}]})JSON";
    }
    const auto animated_anchor = compile_render_plan(*decoded, resolver);
    CHECK_FALSE(animated_anchor);
    if (!animated_anchor)
        CHECK(animated_anchor.error().code == "lottie_unsupported_or_invalid");
}

#if defined(CHRONON_TEST_ENABLE_SVG)
TEST_CASE("render plan imports a resolver-backed SVG before composition compile") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    {
        std::ofstream svg(directory.path / "art/vector.svg");
        svg << R"svg(<svg clip-path="url(#clip)"><!-- comments do not change the rendered subset --><defs>
          <clipPath id="clip"><rect x="0" y="0" width="48" height="48"/></clipPath>
          <linearGradient id="linear" gradientTransform="translate(0.1 0.2)"><stop offset="0" stop-color="#ff0000"><animate attributeName="stop-opacity" from="1" to="0" dur="1s"/></stop><stop offset="1" style="stop-color:blue;stop-opacity:0.5"><animate attributeName="stop-color" from="blue" to="lime" dur="1s"/></stop></linearGradient>
          <radialGradient id="radial" gradientTransform="translate(0.1 0)"><stop offset="0" stop-color="white"/><stop offset="1" stop-color="black"/></radialGradient>
          <path id="reusable" d="M 0 0 L 4 0 L 4 4 Z" fill="lime"/>
          </defs><path d="M 0 0 L 20 0 L 20 20 Z" fill="#ff0000" stroke="#00ff00" stroke-width="2" opacity="0.5"/>
          <path d="M 24 0 L 44 0 L 44 20 Z" fill="#0000ff" stroke="none"/>
          <path d="M 0 24 L 20 24 L 20 44 Z" fill="url(#linear)"/>
          <path d="M 24 24 L 44 24 L 44 44 Z" fill="url(#radial)"/>
          <use href="#reusable" x="5" y="6"/></svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"art/vector.svg"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    CHECK(prepared->assets.assets().size() == 1);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 5);
    CHECK(evaluated->scene.layers()[0].nodes[0].fill.solid.r == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].fill.solid.b == doctest::Approx(0.0f));
    CHECK(evaluated->scene.layers()[0].nodes[1].fill.solid.r == doctest::Approx(0.0f));
    CHECK(evaluated->scene.layers()[0].nodes[1].fill.solid.b == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(0.5f));
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.enabled);
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.width == doctest::Approx(2.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.color.g == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.path().stroke.color.a == doctest::Approx(0.5f));
    CHECK_FALSE(evaluated->scene.layers()[0].nodes[1].shape.path().stroke.enabled);
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.type ==
          graphics::GradientType::Linear);
    REQUIRE(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops.size() == 2);
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[0].color.r == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.a == doctest::Approx(0.5f));
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.start.x == doctest::Approx(2.0f));
    CHECK(evaluated->scene.layers()[0].nodes[2].shape.path().fill.gradient.start.y == doctest::Approx(28.0f));
    CHECK(evaluated->scene.layers()[0].nodes[3].shape.path().fill.gradient.type ==
          graphics::GradientType::Radial);
    CHECK(evaluated->scene.layers()[0].nodes[3].shape.path().fill.gradient.center.x == doctest::Approx(36.0f));
    CHECK(evaluated->scene.layers()[0].nodes[4].shape.path().commands[1].p0.x == doctest::Approx(9.0f));
    CHECK(evaluated->scene.layers()[0].nodes[4].shape.path().commands[1].p0.y == doctest::Approx(6.0f));
    CHECK(evaluated->scene.layers()[0].nodes[4].fill.solid.g == doctest::Approx(1.0f));
    CHECK(evaluated->scene.layers()[0].mask.type == MaskType::Path);
    const auto gradient_middle = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{15});
    const auto gradient_end = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{30});
    REQUIRE(gradient_middle); REQUIRE(gradient_end);
    CHECK(gradient_middle->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.g == doctest::Approx(0.735357f));
    CHECK(gradient_middle->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.b == doctest::Approx(0.735357f));
    CHECK(gradient_middle->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[0].color.a == doctest::Approx(0.5f));
    CHECK(gradient_end->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.g == doctest::Approx(1.0f));
    CHECK(gradient_end->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[1].color.b == doctest::Approx(0.0f));
    CHECK(gradient_end->scene.layers()[0].nodes[2].shape.path().fill.gradient.color_stops[0].color.a == doctest::Approx(0.0f));

    {
        std::ofstream svg(directory.path / "art/masked.svg");
        svg << R"svg(<svg><defs>
          <mask id="alpha" maskUnits="userSpaceOnUse" maskContentUnits="userSpaceOnUse">
            <path d="M 0 0 L 32 0 L 32 32 Z" fill="white"/>
          </mask></defs><path d="M 0 0 L 40 0 L 40 40 Z" fill="red" mask="url(#alpha)"/></svg>)svg";
    }
    auto masked_plan = *decoded;
    masked_plan.layers[0].shape->svg_asset = "art/masked.svg";
    const auto masked = compile_render_plan(masked_plan, resolver);
    REQUIRE(masked);
    const auto masked_frame = evaluate(masked->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    REQUIRE(masked_frame);
    CHECK(masked_frame->scene.layers()[0].mask.type == MaskType::Path);

    auto missing = *decoded;
    missing.layers[0].shape->svg_asset = "art/not-found.svg";
    CHECK_FALSE(compile_render_plan(missing, resolver));

    const std::array<std::string, 15> unsupported_svg{
        "<svg><text>no conversion</text></svg>",
        "<svg><path d=\"M0 0L1 1\" fill=\"url(#gradient)\"/></svg>",
        "<svg><path d=\"M0 0L1 1\"/><g><use href=\"#shape\"/></g></svg>",
        "<svg><defs><linearGradient id=\"g\"/></defs><path d=\"M0 0L1 1\"/></svg>",
        "<svg><mask id=\"m\"><path d=\"M0 0L1 1\"/></mask></svg>",
        "<svg><path d=\"M0 0L1 1\" stroke-linecap=\"round\"/></svg>",
        "<svg><path d=\"M0 0L1 1\" opacity=\"2\"/></svg>",
        "<svg><path d=\"M0 0L1 1\"><animate attributeName=\"opacity\" from=\"0\" to=\"1\" dur=\"1s\" repeatCount=\"indefinite\"/></path></svg>",
        "<svg><g><animate attributeName=\"fill\" from=\"red\" to=\"blue\" dur=\"1s\"/><path d=\"M0 0L1 1\"/></g></svg>",
        "<svg><path d=\"M0 0L1 1\"><animateTransform attributeName=\"transform\" type=\"scale\" from=\"1 2\" to=\"3 4\" dur=\"1s\"/></path></svg>",
        "<svg><path d=\"M0 0L1 1\"><animate attributeName=\"opacity\" from=\"0\" to=\"1\" dur=\"1.01s\"/></path></svg>",
        "<svg><g transform=\"translate(2 3)\"><path d=\"M0 0L1 1\"><animateTransform attributeName=\"transform\" type=\"translate\" from=\"0 0\" to=\"1 1\" dur=\"1s\"/></path></g></svg>",
        "<svg><use href=\"#missing\"/></svg>",
        "<svg><defs><g id=\"cycle\"><use href=\"#cycle\"/></g></defs><use href=\"#cycle\"/></svg>",
        "<svg><path d=\"M0 0L1 0\"><animate attributeName=\"d\" values=\"M0 0L1 0;M0 0L1 1L2 2\" dur=\"1s\"/></path></svg>"};
    for (std::size_t i = 0; i < unsupported_svg.size(); ++i) {
        const std::string logical = "art/rejected-" + std::to_string(i) + ".svg";
        { std::ofstream svg(directory.path / logical); svg << unsupported_svg[i]; }
        auto rejected = *decoded;
        rejected.layers[0].shape->svg_asset = logical;
        const auto result = compile_render_plan(rejected, resolver);
        CHECK_FALSE(result);
        if (!result) {
            const bool expected_error = result.error().code == "svg_import_failed" ||
                result.error().code == "svg_unsupported_style" ||
                result.error().code == "svg_animation_timing_unsupported";
            CHECK(expected_error);
        }
    }
}

TEST_CASE("render plan lowers SVG opacity and translate animations into native sampled paths") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    {
        std::ofstream svg(directory.path / "art/animated.svg");
        svg << R"svg(<svg><path d="M 0 0 L 10 0" fill="red" stroke="blue" stroke-width="2">
          <animateTransform attributeName="transform" type="translate" from="0 0" to="20 0" dur="1s" fill="freeze"/>
          <animateTransform attributeName="transform" type="scale" from="1 1" to="2 2" dur="1s" fill="freeze"/>
          <animateTransform attributeName="transform" type="rotate" from="0" to="90" dur="1s" fill="freeze"/>
          <animate attributeName="opacity" from="1" to="0.5" dur="1s" fill="freeze"/>
          <animate attributeName="fill" from="red" to="white" dur="1s" fill="freeze"/>
          <animate attributeName="stroke" from="blue" to="lime" dur="1s" fill="freeze"/>
          <animate attributeName="stroke-width" from="2" to="6" dur="1s" fill="freeze"/>
          <animate attributeName="fill-opacity" from="1" to="0.5" dur="1s" fill="freeze"/>
          <animate attributeName="stroke-opacity" from="1" to="0.5" dur="1s" fill="freeze"/>
        </path><g>
          <animateTransform attributeName="transform" type="translate" from="0 0" to="10 0" dur="1s" fill="freeze"/>
          <animate attributeName="opacity" from="1" to="0.5" dur="1s" fill="freeze"/>
          <path d="M 0 10 L 10 10" fill="blue"/>
        </g><path d="M 0 30 L 10 30" fill="red">
          <animate attributeName="d" values="M 0 30 L 10 30;M 0 30 L 20 30;M 0 30 L 30 30" dur="1s"/>
        </path></svg>)svg";
    }
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    auto decoded = decode_render_plan(nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":31},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"art/animated.svg"}}],
      "output":{"path":"out.png"}})JSON"));
    REQUIRE(decoded);
    const auto prepared = compile_render_plan(*decoded, resolver);
    const auto prepare_error = prepared.has_value() ? std::string{} :
        prepared.error().path + ": " + prepared.error().message;
    REQUIRE_MESSAGE(prepared.has_value(), prepare_error);
    const auto first = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    const auto middle = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{15});
    const auto last = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{30});
    REQUIRE(first); REQUIRE(middle); REQUIRE(last);
    REQUIRE(first->scene.layers()[0].nodes.size() == 3);
    CHECK(first->scene.layers()[0].nodes[1].shape.path().commands[1].p0.x == doctest::Approx(10.0f));
    CHECK(middle->scene.layers()[0].nodes[1].shape.path().commands[1].p0.x == doctest::Approx(15.0f));
    CHECK(last->scene.layers()[0].nodes[1].shape.path().commands[1].p0.x == doctest::Approx(20.0f));
    CHECK(middle->scene.layers()[0].nodes[1].fill.solid.a == doctest::Approx(0.75f));
    CHECK(first->scene.layers()[0].nodes[2].shape.path().commands[1].p0.x == doctest::Approx(10.0f));
    CHECK(middle->scene.layers()[0].nodes[2].shape.path().commands[1].p0.x == doctest::Approx(20.0f));
    CHECK(last->scene.layers()[0].nodes[2].shape.path().commands[1].p0.x == doctest::Approx(30.0f));
    CHECK(first->scene.layers()[0].nodes[0].shape.path().commands[1].p0.x == doctest::Approx(10.0f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().commands[1].p0.x == doctest::Approx(20.6066f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().commands[1].p0.y == doctest::Approx(10.6066f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().commands[1].p0.x == doctest::Approx(20.0f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().commands[1].p0.y == doctest::Approx(20.0f));
    CHECK(first->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(1.0f));
    CHECK(middle->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(0.5625f));
    CHECK(last->scene.layers()[0].nodes[0].fill.solid.a == doctest::Approx(0.25f));
    CHECK(first->scene.layers()[0].nodes[0].fill.solid.g == doctest::Approx(0.0f));
    CHECK(middle->scene.layers()[0].nodes[0].fill.solid.g == doctest::Approx(0.5f));
    CHECK(last->scene.layers()[0].nodes[0].fill.solid.g == doctest::Approx(1.0f));
    CHECK(first->scene.layers()[0].nodes[0].shape.path().stroke.color.b == doctest::Approx(1.0f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().stroke.color.g == doctest::Approx(0.5f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().stroke.color.g == doctest::Approx(1.0f));
    CHECK(first->scene.layers()[0].nodes[0].shape.path().stroke.width == doctest::Approx(2.0f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().stroke.width == doctest::Approx(4.0f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().stroke.width == doctest::Approx(6.0f));
    CHECK(middle->scene.layers()[0].nodes[0].shape.path().stroke.color.a == doctest::Approx(0.5625f));
    CHECK(last->scene.layers()[0].nodes[0].shape.path().stroke.color.a == doctest::Approx(0.25f));
}
#endif

TEST_CASE("all text backends share one linear-domain color packer") {
    const chronon3d::Color color{0.2158605f, 0.5f, 1.0f, 0.5f};
    const auto packed = chronon3d::text::pack_text_color(color);
    CHECK((packed & 0xFFu) == 55u);
    CHECK(((packed >> 8) & 0xFFu) == 128u);
    CHECK(((packed >> 16) & 0xFFu) == 255u);
    CHECK(((packed >> 24) & 0xFFu) == 128u);
}

TEST_CASE("compile_render_plan fails loudly when the budget is exceeded") {
    chronon3d::render_plan::RenderPlan plan;
    plan.canvas = {.width = 0, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{1}};
    chronon3d::assets::AssetResolver resolver;
    const auto result = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE_FALSE(result);
    CHECK(result.error().path == "canvas.width");
}

TEST_CASE("prepared render plan owns the canonical compiled composition") {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "prepared-plan-test";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{3}};

    chronon3d::render_plan::LayerPlan color;
    color.id = "background";
    color.type = chronon3d::render_plan::LayerType::Color;
    color.color = {0.1f, 0.2f, 0.3f, 1.0f};
    plan.layers.push_back(color);

    chronon3d::assets::AssetResolver resolver;
    const auto prepared_result =
        chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared_result);
    const auto& prepared = prepared_result.value();

    REQUIRE(prepared.compiled_composition.composition);
    REQUIRE(prepared.compiled_composition.asset_manifest);
    CHECK(prepared.compiled_composition.asset_manifest->assets().empty());
    CHECK(prepared.compiled_composition.composition->name() ==
          "prepared-plan-test");
    CHECK(prepared.compiled_composition.composition->width() == 320);
    CHECK(prepared.compiled_composition.fingerprint != 0);
    REQUIRE(prepared.composition);
    CHECK(prepared.composition->width() ==
          prepared.compiled_composition.composition->width());

    // The compiled value owns its definition.  Dropping the source plan and
    // resolver cannot invalidate the scene callback or its metadata.
    const auto compiled = prepared.compiled_composition;
    const auto evaluated = chronon3d::evaluate(
        compiled, chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{1});
    REQUIRE(evaluated);
    CHECK(evaluated->scene.layers().size() == 1);

    chronon3d::assets::AssetResolver second_resolver;
    const auto repeated =
        chronon3d::render_plan::compile_render_plan(plan, second_resolver);
    REQUIRE(repeated);
    CHECK(repeated->compiled_composition.fingerprint ==
          prepared.compiled_composition.fingerprint);
    CHECK(repeated->fingerprint.content_digest == prepared.fingerprint.content_digest);
    CHECK(repeated->fingerprint.request_digest == prepared.fingerprint.request_digest);
}

TEST_CASE("render plan compiler transfers safe insets to layer layout rules") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "safe-insets-layout";
    plan.canvas = {.width = 1080, .height = 1920,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan shape;
    shape.id = "caption-card";
    shape.type = LayerType::Shape;
    shape.size = {800.0f, 180.0f};
    shape.size_dimensions = 2;
    shape.position = {900.0f, 1800.0f, 0.0f};
    shape.position_dimensions = 2;
    shape.safe_insets = std::array<float, 4>{80.0f, 60.0f, 220.0f, 60.0f};
    shape.shape = ShapeContentPlan{};
    shape.shape->type = ShapeContentTypePlan::Rect;
    shape.shape->fill_color = std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
    plan.layers.push_back(shape);

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    CompositionEvaluateContext evaluate_context;
    evaluate_context.frame_context = make_frame_context(FrameContextParams{
        .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
        .duration = Frame{1}, .width = 1080, .height = 1920});
    const auto evaluated = chronon3d::evaluate(
        prepared->compiled_composition, evaluate_context, chronon3d::Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& layout = evaluated->scene.layers()[0].layout;
    CHECK(layout.enabled);
    CHECK(layout.keep_in_safe_area);
    CHECK(layout.safe_area.top == doctest::Approx(-880.0f));
    CHECK(layout.safe_area.right == doctest::Approx(1400.0f));
    CHECK(layout.safe_area.bottom == doctest::Approx(1360.0f));
    CHECK(layout.safe_area.left == doctest::Approx(-480.0f));
    CHECK(evaluated->scene.layers()[0].transform.position.x == doctest::Approx(-320.0f));
    CHECK(evaluated->scene.layers()[0].transform.position.y == doctest::Approx(560.0f));
}

TEST_CASE("render plan compiles semantic word fill-color animator") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "semantic-word-fill";
    plan.canvas = {.width = 320, .height = 120,
                   .fps = FrameRate{30, 1}, .duration = Frame{10}};
    LayerPlan text;
    text.id = "caption";
    text.type = LayerType::Text;
    text.text = "Hi future";
    text.font = "tests/fixtures/Inter-Bold.ttf";
    LayerStylePlan style;
    style.font = text.font;
    style.font_size = 32.0f;
    style.fill = "#FFFFFF";
    text.style = style;
    TextSpanOverride word_span;
    word_span.byte_start = 3;
    word_span.byte_end = 9;
    word_span.semantic_id = "word-0";
    text.text_spans.push_back(word_span);

    TextSelectorPlan selector;
    selector.semantic_id = "word-0";
    selector.shape = "square";
    selector.amount.keyframes = {
        {Frame{0}, {0.0f}}, {Frame{8}, {100.0f}}};
    TextAnimatorPlan animator;
    animator.id = "word-highlight";
    animator.selectors.push_back(selector);
    AnimationTrackPlan fill_color;
    fill_color.property = "fill_color";
    fill_color.keyframes = {{Frame{0}, {1.0f, 0.7f, 0.0f, 1.0f}}};
    animator.properties.push_back(fill_color);
    text.text_animators.push_back(animator);
    plan.layers.push_back(text);

    const auto source_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path();
    assets::AssetResolver resolver;
    resolver.mount(source_root);
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{4});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    const auto shape = evaluated->scene.layers()[0].nodes[0]
        .shape.text_run_shape_handle().value;
    REQUIRE(shape);
    REQUIRE(shape->animators.size() == 1);
    REQUIRE(shape->animators[0].properties.size() == 1);
    const auto* target = std::get_if<FillColorProperty>(
        &shape->animators[0].properties[0]);
    REQUIRE(target);
    const auto evaluated_color = target->color.evaluate(4.0);
    CHECK(evaluated_color.r == doctest::Approx(1.0f));
    CHECK(evaluated_color.g == doctest::Approx(0.7f));
    CHECK(evaluated_color.b == doctest::Approx(0.0f));
}

TEST_CASE("render plan evaluates animated solid fills and typed transitions per frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-solid-transition";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{12}};
    LayerPlan shape;
    shape.id = "paint";
    shape.type = LayerType::Shape;
    shape.size = {24.0f, 24.0f};
    shape.size_dimensions = 2;
    shape.shape = ShapeContentPlan{};
    shape.shape->type = ShapeContentTypePlan::Rect;
    shape.shape->fill_color = std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
    AnimationTrackPlan fill;
    fill.property = "fill_color";
    fill.keyframes = {{Frame{0}, {0.0f, 0.0f, 0.0f, 1.0f}},
                      {Frame{10}, {1.0f, 0.0f, 0.0f, 1.0f}}};
    shape.animation = AnimationPlan{.tracks = {fill}};
    TransitionPlan transition;
    transition.id = "slide";
    transition.kind = "slide";
    transition.duration_frames = Frame{15};  // 0.5 s at 30 fps
    transition.direction = "left";
    transition.distance = 0.5f;
    shape.transition_in = transition;
    plan.layers.push_back(shape);

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto frame0 = evaluate(prepared->compiled_composition,
                                 CompositionEvaluateContext{}, Frame{0});
    const auto frame5 = evaluate(prepared->compiled_composition,
                                 CompositionEvaluateContext{}, Frame{5});
    REQUIRE(frame0);
    REQUIRE(frame5);
    REQUIRE(frame0->scene.layers().size() == 1);
    REQUIRE(frame5->scene.layers().size() == 1);
    const auto& layer0 = frame0->scene.layers()[0];
    const auto& layer5 = frame5->scene.layers()[0];
    REQUIRE(layer0.nodes.size() == 1);
    REQUIRE(layer5.nodes.size() == 1);
    CHECK(layer0.nodes[0].fill.solid.r == doctest::Approx(0.0f));
    CHECK(layer5.nodes[0].fill.solid.r == doctest::Approx(0.5f));
    CHECK(layer_transition_kind(layer0.transition_in.transition_id) ==
          LayerTransitionKind::Slide);
    CHECK(std::get<SlideParams>(layer0.transition_in.parameters).distance == doctest::Approx(0.5f));
}

TEST_CASE("render plan evaluates animated path mask geometry per frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-path-mask";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{12}};
    LayerPlan layer;
    layer.id = "masked";
    layer.type = LayerType::Color;
    layer.color = {1.0f, 0.0f, 0.0f, 1.0f};
    LayerPlan::PathMaskPlan mask;
    const auto rect_path = [](float extent) {
        return std::vector<ShapePathCommandPlan>{
            {ShapePathCommandTypePlan::MoveTo, {-extent, -extent}},
            {ShapePathCommandTypePlan::LineTo, {extent, -extent}},
            {ShapePathCommandTypePlan::LineTo, {extent, extent}},
            {ShapePathCommandTypePlan::LineTo, {-extent, extent}},
            {ShapePathCommandTypePlan::Close}};
    };
    mask.path = rect_path(4.0f);
    mask.morph_target = rect_path(8.0f);
    AnimationTrackPlan animation;
    animation.property = "mask_morph";
    animation.keyframes = {{Frame{0}, {0.0f}}, {Frame{12}, {1.0f}}};
    mask.morph_animation = animation;
    layer.path_mask = mask;
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto start = evaluate(prepared->compiled_composition,
                                CompositionEvaluateContext{}, Frame{0});
    const auto end = evaluate(prepared->compiled_composition,
                              CompositionEvaluateContext{}, Frame{12});
    REQUIRE(start);
    REQUIRE(end);
    REQUIRE(start->scene.layers().size() == 1);
    REQUIRE(end->scene.layers().size() == 1);
    CHECK(start->scene.layers()[0].mask.size.x == doctest::Approx(8.0f));
    CHECK(end->scene.layers()[0].mask.size.x == doctest::Approx(16.0f));
}

TEST_CASE("render plan lowers ordered mask operations to the scene compositor") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "ordered-mask-stack";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "masked";
    layer.type = LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};
    const auto rect = [](std::string mode, float extent) {
        LayerPlan::PathMaskPlan mask;
        mask.type = "rect";
        mask.mode = std::move(mode);
        mask.size = {extent * 2.0f, extent * 2.0f};
        return mask;
    };
    const auto ellipse = [](std::string mode, float rx, float ry) {
        LayerPlan::PathMaskPlan mask;
        mask.type = "ellipse";
        mask.mode = std::move(mode);
        mask.size = {rx * 2.0f, ry * 2.0f};
        return mask;
    };
    auto path = LayerPlan::PathMaskPlan{};
    path.type = "path";
    path.mode = "add";
    path.path = {{ShapePathCommandTypePlan::MoveTo, {-1.0f, -1.0f}},
                 {ShapePathCommandTypePlan::LineTo, {1.0f, -1.0f}},
                 {ShapePathCommandTypePlan::LineTo, {1.0f, 1.0f}},
                 {ShapePathCommandTypePlan::LineTo, {-1.0f, 1.0f}},
                 {ShapePathCommandTypePlan::Close}};
    auto rounded_rect = rect("add", 3.0f);
    rounded_rect.type = "rounded_rect";
    rounded_rect.radius = 1.0f;
    layer.masks = {rect("add", 20.0f), rect("subtract", 4.0f), path,
                   ellipse("add", 2.0f, 2.0f), rounded_rect,
                   ellipse("intersect", 30.0f, 30.0f), rect("difference", 1.0f)};
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& mask = evaluated->scene.layers()[0].mask;
    REQUIRE(mask.operations.size() == 6);
    CHECK(mask.operations[0].mode == MaskCombineMode::Subtract);
    CHECK(mask.operations[1].type == MaskType::Path);
    CHECK(mask.operations[2].type == MaskType::Ellipse);
    CHECK(mask.operations[3].type == MaskType::RoundedRect);
    CHECK(mask.operations[4].mode == MaskCombineMode::Intersect);
    CHECK(mask.operations[5].mode == MaskCombineMode::Difference);
    CHECK(mask_alpha_at_local_point(mask, Vec2{0.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(mask_alpha_at_local_point(mask, Vec2{8.0f, 0.0f}) == doctest::Approx(1.0f));
}

TEST_CASE("render plan fill and gradient tracks clamp eased values and hold as steps") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    LayerPlan solid;
    solid.type = LayerType::Color;
    solid.color = {0.0f, 0.0f, 1.0f, 1.0f};
    AnimationTrackPlan fill;
    fill.property = "fill_color";
    fill.easing = "hold";
    fill.keyframes = {{Frame{5}, {1.0f, 0.0f, 0.0f, 1.0f}},
                      {Frame{10}, {0.0f, 1.0f, 0.0f, 1.0f}}};
    solid.animation = AnimationPlan{.tracks = {fill}};
    const auto before = chronon3d::render_plan::detail::fill_color_at(
        solid, 2.0, Color{0.0f, 0.0f, 1.0f, 1.0f}, FrameRate{30, 1});
    REQUIRE(before.has_value());
    CHECK(before->b == doctest::Approx(1.0f));
    const auto held = chronon3d::render_plan::detail::fill_color_at(
        solid, 7.0, Color{0.0f, 0.0f, 1.0f, 1.0f}, FrameRate{30, 1});
    REQUIRE(held.has_value());
    CHECK(held->r == doctest::Approx(1.0f));
    CHECK(held->g == doctest::Approx(0.0f));
    const auto last = chronon3d::render_plan::detail::fill_color_at(
        solid, 10.0, Color{0.0f, 0.0f, 1.0f, 1.0f}, FrameRate{30, 1});
    REQUIRE(last.has_value());
    CHECK(last->g == doctest::Approx(1.0f));

    const auto gradient = [](Color first) {
        return graphics::GradientDefinition::linear(
            {0.0f, 0.0f}, {1.0f, 0.0f},
            {{0.0f, first}, {1.0f, Color::white()}},
            graphics::GradientSpread::Pad);
    };
    LayerPlan gradient_layer;
    gradient_layer.type = LayerType::Shape;
    gradient_layer.shape = ShapeContentPlan{};
    gradient_layer.shape->gradient = gradient(Color::blue());
    GradientAnimationPlan gradient_track;
    gradient_track.easing = "hold";
    gradient_track.keyframes = {
        {Frame{5}, gradient(Color::red())},
        {Frame{10}, gradient(Color::green())}};
    gradient_layer.fill_gradient_animation = gradient_track;
    const auto gradient_before = chronon3d::render_plan::detail::fill_gradient_at(gradient_layer, 2.0);
    REQUIRE(gradient_before.has_value());
    CHECK(gradient_before->color_stops[0].color.b == doctest::Approx(1.0f));
    const auto gradient_held = chronon3d::render_plan::detail::fill_gradient_at(gradient_layer, 7.0);
    REQUIRE(gradient_held.has_value());
    CHECK(gradient_held->color_stops[0].color.r == doctest::Approx(1.0f));
    CHECK(gradient_held->color_stops[0].color.g == doctest::Approx(0.0f));
}

TEST_CASE("render plan compiles a GradientMesh fill to the canonical path paint") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "gradient-mesh-fill";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan shape;
    shape.id = "mesh";
    shape.type = LayerType::Shape;
    shape.size = {32.0f, 32.0f};
    shape.size_dimensions = 2;
    shape.shape = ShapeContentPlan{};
    shape.shape->type = ShapeContentTypePlan::Rect;
    shape.shape->gradient_mesh = graphics::GradientMesh::four_corner(
        Color::red(), Color::green(), Color::blue(), Color::white());
    plan.layers.push_back(std::move(shape));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    const auto& authored_mesh = plan.layers[0].shape->gradient_mesh;
    REQUIRE(authored_mesh);
    REQUIRE(authored_mesh->nodes.size() == 4);
    CHECK(authored_mesh->nodes[0].color.r == doctest::Approx(1.0f));
    CHECK(authored_mesh->nodes[1].color.g == doctest::Approx(1.0f));
    CHECK(authored_mesh->nodes[2].color.b == doctest::Approx(1.0f));
}

TEST_CASE("render plan evaluates animated gradient color ramps in authored sRGB") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-gradient-ramp";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{10}};
    const auto make_gradient = [](Color first, Color last, float start_x) {
        return graphics::GradientDefinition::linear(
            {start_x, 0.0f}, {1.0f, 0.0f},
            {{0.0f, first}, {1.0f, last}}, graphics::GradientSpread::Reflect);
    };
    LayerPlan shape;
    shape.id = "gradient";
    shape.type = LayerType::Shape;
    shape.size = {32.0f, 32.0f};
    shape.size_dimensions = 2;
    shape.shape = ShapeContentPlan{};
    shape.shape->type = ShapeContentTypePlan::Rect;
    shape.shape->gradient = make_gradient(Color::red(), Color::blue(), 0.0f);
    GradientAnimationPlan animation;
    animation.keyframes = {
        {Frame{0}, make_gradient(Color::red(), Color::blue(), 0.0f)},
        {Frame{10}, make_gradient(Color::green(), Color::white(), 0.5f)}};
    shape.fill_gradient_animation = animation;
    plan.layers.push_back(shape);

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
                                    CompositionEvaluateContext{}, Frame{5});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    const auto& fill = evaluated->scene.layers()[0].nodes[0].fill;
    REQUIRE(fill.gradient.type == graphics::GradientType::Linear);
    CHECK(fill.gradient.start.x == doctest::Approx(0.25f));
    REQUIRE(fill.gradient.color_stops.size() == 2);
    CHECK(fill.gradient.color_stops[0].color.r == doctest::Approx(0.7353569f).epsilon(0.002f));
    CHECK(fill.gradient.color_stops[0].color.g == doctest::Approx(0.7353569f).epsilon(0.002f));
}

TEST_CASE("render plan resolves grow-and-shrink text height before evaluation") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "responsive-caption";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan text;
    text.id = "caption";
    text.type = LayerType::Text;
    text.text = "A longer caption wraps to multiple lines";
    text.font = "tests/fixtures/Inter-Bold.ttf";
    text.size = {160.0f, 12.0f};
    text.size_dimensions = 2;
    LayerStylePlan style;
    style.font = text.font;
    style.font_size = 24.0f;
    style.fill = "#FFFFFF";
    style.text_fit_mode = "grow_and_shrink";
    style.min_font_size = 12.0f;
    style.max_font_size = 32.0f;
    text.style = style;
    plan.layers.push_back(std::move(text));

    const auto source_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path();
    assets::AssetResolver resolver;
    resolver.mount(source_root);
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    const auto handle = evaluated->scene.layers()[0].nodes[0]
        .shape.text_run_shape_handle().value;
    REQUIRE(handle);
    CHECK(handle->layout_spec.box.x == doctest::Approx(160.0f));
    CHECK(handle->layout_spec.box.y > 12.0f);
    CHECK(handle->layout_spec.auto_fit == false);
}

TEST_CASE("render plan text background becomes a measured card behind the text run") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "caption-card";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan text;
    text.id = "caption";
    text.type = LayerType::Text;
    text.text = "A caption that wraps into multiple lines";
    text.font = "tests/fixtures/Inter-Bold.ttf";
    text.size = {170.0f, 16.0f};
    text.size_dimensions = 2;
    LayerStylePlan style;
    style.font = text.font;
    style.font_size = 24.0f;
    style.fill = "#FFFFFF";
    style.text_fit_mode = "grow_and_shrink";
    style.min_font_size = 12.0f;
    style.background = BackgroundStyle{
        .color = "#101820", .opacity = 0.9f, .radius = 6.0f,
        .padding = {10.0f, 8.0f}, .padding_dimensions = 2};
    text.style = style;
    plan.layers.push_back(std::move(text));

    const auto source_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path();
    assets::AssetResolver resolver;
    resolver.mount(source_root);
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& nodes = evaluated->scene.layers()[0].nodes;
    REQUIRE(nodes.size() == 2);
    CHECK(nodes[0].shape.type() == ShapeType::RoundedRect);
    CHECK(nodes[1].shape.type() == ShapeType::TextRun);
    CHECK(nodes[0].shape.rounded_rect().size.y > 16.0f);
    const auto run = nodes[1].shape.text_run_shape_handle().value;
    REQUIRE(run);
    CHECK(run->layout_spec.box.y > 16.0f);
}

TEST_CASE("render plan composes synchronized X and Y position tracks") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "component-position-tracks";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{25}};
    LayerPlan shape;
    shape.id = "moving-shape";
    shape.type = LayerType::Shape;
    shape.size = {20.0f, 20.0f};
    shape.size_dimensions = 2;
    shape.shape = ShapeContentPlan{};
    AnimationPlan animation;
    AnimationTrackPlan x;
    x.property = "position_x";
    x.keyframes = {{Frame{0}, {0.0f}}, {Frame{24}, {480.0f}}};
    AnimationTrackPlan y;
    y.property = "position_y";
    y.keyframes = {{Frame{0}, {0.0f}}, {Frame{24}, {240.0f}}};
    animation.tracks = {x, y};
    shape.animation = std::move(animation);
    plan.layers.push_back(std::move(shape));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    const auto initial = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(initial);
    REQUIRE(evaluated->scene.layers().size() == 1);
    CHECK(evaluated->scene.layers()[0].transform.position.x -
          initial->scene.layers()[0].transform.position.x == doctest::Approx(240.0f));
    CHECK(evaluated->scene.layers()[0].transform.position.y -
          initial->scene.layers()[0].transform.position.y == doctest::Approx(120.0f));
}

TEST_CASE("render plan evaluates animated trim paths at the requested frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-trim-path";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{30, 1}, .duration = Frame{11}};
    LayerPlan layer;
    layer.id = "progress-route";
    layer.type = LayerType::Shape;
    layer.size = {100.0f, 1.0f};
    layer.size_dimensions = 2;
    ShapeContentPlan path;
    path.type = ShapeContentTypePlan::Path;
    path.path = {
        ShapePathCommandPlan{ShapePathCommandTypePlan::MoveTo, {0.0f, 0.0f}},
        ShapePathCommandPlan{ShapePathCommandTypePlan::LineTo, {100.0f, 0.0f}}};
    PathOperatorPlan trim;
    trim.kind = PathOperatorKindPlan::Trim;
    trim.trim_start = 0.0f;
    trim.trim_end = 0.1f;
    AnimationTrackPlan animation;
    animation.property = "trim";
    animation.keyframes = {
        AnimationKeyframePlan{Frame{0}, {0.0f, 0.1f}},
        AnimationKeyframePlan{Frame{10}, {0.0f, 0.75f}}};
    trim.trim_animation = animation;
    path.operators.push_back(std::move(trim));
    layer.shape = std::move(path);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto at_start = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    const auto at_end = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{10});
    REQUIRE(at_start);
    REQUIRE(at_end);
    REQUIRE(at_start->scene.layers()[0].nodes[0].shape.path().commands.size() >= 2);
    REQUIRE(at_end->scene.layers()[0].nodes[0].shape.path().commands.size() >= 2);
    CHECK(at_start->scene.layers()[0].nodes[0].shape.path().commands.back().p0.x ==
          doctest::Approx(10.0f).epsilon(0.02f));
    CHECK(at_end->scene.layers()[0].nodes[0].shape.path().commands.back().p0.x ==
          doctest::Approx(75.0f).epsilon(0.02f));
}

TEST_CASE("render plan lowers star, polygon, arrow, and arc to prepared path nodes") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    const std::vector<nlohmann::json> shapes{
        {{"type", "star"}, {"points", 7}, {"inner_radius_ratio", 0.4}},
        {{"type", "polygon"}, {"points", 6}, {"rotation_degrees", 15.0}},
        {{"type", "arrow"}, {"shaft_ratio", 0.3}, {"head_ratio", 0.8}},
        {{"type", "arc"}, {"start_degrees", 10.0}, {"sweep_degrees", 180.0},
         {"segments", 32}, {"stroke", {{"color", "#FFFFFF"}, {"width", 2.0}}}},
        {{"type", "grid"}, {"spacing", 10.0},
         {"stroke", {{"color", "#FFFFFF"}, {"width", 1.0}}}},
        {{"type", "dot_grid"}, {"spacing", 10.0}, {"dot_radius", 2.0},
         {"fill", {1.0, 0.0, 0.0, 1.0}}}
    };
    const std::array<std::size_t, 6> minimum_commands{15, 7, 8, 17, 18, 100};
    assets::AssetResolver resolver;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const auto source = render_plan_test::base_plan(
            {render_plan_test::shape_layer(shapes[i])});
        const auto decoded = decode_render_plan(source);
        REQUIRE(decoded);
        const auto prepared = compile_render_plan(*decoded, resolver);
        if (!prepared) INFO(prepared.error().message);
        REQUIRE(prepared);
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{0});
        REQUIRE(evaluated);
        REQUIRE(evaluated->scene.layers().size() == 1);
        REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
        const auto& path = evaluated->scene.layers()[0].nodes[0].shape;
        CHECK(path.type() == ShapeType::Path);
        CHECK(path.path().commands.size() >= minimum_commands[i]);
    }
}

TEST_CASE("device frames lower real phone and browser chrome subnodes") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    const std::array<std::pair<nlohmann::json, std::size_t>, 2> cases{{
        {nlohmann::json{{"type", "device_frame"}, {"device", "phone"}, {"chrome", true},
                        {"fill", {0.2, 0.3, 0.4, 1.0}}}, 3},
        {nlohmann::json{{"type", "device_frame"}, {"device", "browser"}, {"chrome", true},
                        {"fill", {0.2, 0.3, 0.4, 1.0}}}, 6}
    }};
    assets::AssetResolver resolver;
    for (const auto& [shape, expected_nodes] : cases) {
        const auto source = render_plan_test::base_plan(
            {render_plan_test::shape_layer(shape)});
        const auto decoded = decode_render_plan(source);
        REQUIRE(decoded);
        const auto prepared = compile_render_plan(*decoded, resolver);
        if (!prepared) INFO(prepared.error().message);
        REQUIRE(prepared);
        const auto evaluated = evaluate(prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{0});
        REQUIRE(evaluated);
        REQUIRE(evaluated->scene.layers().size() == 1);
        CHECK(evaluated->scene.layers()[0].nodes.size() == expected_nodes);
    }
}

TEST_CASE("Directional and Radial Blur lower to their registered processor payloads") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "blur-effects";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "source";
    layer.type = LayerType::Color;
    layer.color = {0.2f, 0.3f, 0.4f, 1.0f};
    LayerPlan::EffectPlan directional;
    directional.kind = LayerPlan::EffectKindPlan::DirectionalBlur;
    directional.directional_angle = 22.0f;
    directional.directional_length = 18.0f;
    directional.directional_samples = 9;
    LayerPlan::EffectPlan radial;
    radial.kind = LayerPlan::EffectKindPlan::RadialBlur;
    radial.radial_center = {0.25f, 0.75f};
    radial.radial_amount = 0.8f;
    radial.radial_render_samples = 12;
    radial.radial_preview_samples = 6;
    layer.effects = {directional, radial};
    plan.layers.push_back(std::move(layer));
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 2);
    const auto* lowered_directional = std::get_if<DirectionalBlurParams>(&effects[0].params);
    REQUIRE(lowered_directional);
    CHECK(lowered_directional->length == doctest::Approx(18.0f));
    const auto* lowered_radial = std::get_if<RadialBlurParams>(&effects[1].params);
    REQUIRE(lowered_radial);
    CHECK(lowered_radial->center.y == doctest::Approx(0.75f));
    CHECK(lowered_radial->render_samples == 12);
}

TEST_CASE("render plan spring tracks match the canonical spring sample") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "spring-position-track";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{30, 1}, .duration = Frame{11}};
    LayerPlan layer;
    layer.id = "spring-layer";
    layer.type = LayerType::Shape;
    layer.size = {8.0f, 8.0f};
    layer.size_dimensions = 2;
    layer.position_dimensions = 2;
    layer.shape = ShapeContentPlan{};
    AnimationTrackPlan track;
    track.property = "position_y";
    track.easing = "spring";
    track.spring = Spring::Bouncy;
    track.keyframes = {
        AnimationKeyframePlan{Frame{0}, {-300.0f}},
        AnimationKeyframePlan{Frame{10}, {0.0f}}};
    AnimationPlan animation;
    animation.tracks.push_back(track);
    layer.animation = std::move(animation);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto start = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    const auto middle = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{5});
    REQUIRE(start);
    REQUIRE(middle);
    const float actual_delta = middle->scene.layers()[0].transform.position.y -
                               start->scene.layers()[0].transform.position.y;
    const float expected = sample_spring(5.0 / 30.0, -300.0f, 0.0f, Spring::Bouncy).value + 300.0f;
    CHECK(actual_delta == doctest::Approx(expected).epsilon(0.001f));
}

TEST_CASE("render plan forwards cycle, pingpong and offset loop modes to evaluation") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    auto sample = [](std::string mode, Frame frame) -> std::optional<float> {
        RenderPlan plan;
        plan.job_id = "loop-mode-track";
        plan.canvas = {.width = 320, .height = 180,
                       .fps = FrameRate{30, 1}, .duration = Frame{31}};
        LayerPlan layer;
        layer.id = "looping-shape";
        layer.type = LayerType::Shape;
        layer.size = {8.0f, 8.0f};
        layer.size_dimensions = 2;
        layer.shape = ShapeContentPlan{};
        AnimationTrackPlan track;
        track.property = "position_x";
        track.loop_mode = std::move(mode);
        track.keyframes = {{Frame{0}, {0.0f}}, {Frame{10}, {20.0f}}};
        AnimationPlan animation;
        animation.tracks.push_back(std::move(track));
        layer.animation = std::move(animation);
        plan.layers.push_back(std::move(layer));

        assets::AssetResolver resolver;
        const auto prepared = compile_render_plan(plan, resolver);
        if (!prepared) return std::nullopt;
        const auto evaluated = evaluate(
            prepared->compiled_composition, CompositionEvaluateContext{}, frame);
        if (!evaluated) return std::nullopt;
        return evaluated->scene.layers()[0].transform.position.x;
    };

    const auto origin = sample("cycle", Frame{0});
    const auto cycle = sample("cycle", Frame{15});
    const auto pingpong = sample("pingpong", Frame{15});
    const auto offset = sample("offset", Frame{15});
    REQUIRE(origin);
    REQUIRE(cycle);
    REQUIRE(pingpong);
    REQUIRE(offset);
    CHECK(*cycle - *origin == doctest::Approx(10.0f));
    CHECK(*pingpong - *origin == doctest::Approx(10.0f));
    CHECK(*offset - *origin == doctest::Approx(30.0f));
}

TEST_CASE("render plan wiggle driver evaluates deterministically on scalar opacity") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "wiggle-opacity-track";
    plan.canvas = {.width = 32, .height = 32,
                   .fps = FrameRate{30, 1}, .duration = Frame{31}};
    LayerPlan layer;
    layer.id = "wiggle-shape";
    layer.type = LayerType::Shape;
    layer.size = {8.0f, 8.0f};
    layer.size_dimensions = 2;
    layer.shape = ShapeContentPlan{};
    AnimationTrackPlan track;
    track.property = "opacity";
    track.keyframes = {{Frame{0}, {0.5f}}, {Frame{30}, {0.5f}}};
    track.wiggle = AnimationTrackPlan::WiggleDriverPlan{2.0f, 0.2f, 42};
    AnimationPlan animation;
    animation.tracks.push_back(std::move(track));
    layer.animation = std::move(animation);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto first = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{15});
    const auto repeated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{15});
    REQUIRE(first);
    REQUIRE(repeated);
    const double expected = 0.5 + math::evaluate_expression(
        "wiggle(2,0.2,time,42)",
        std::unordered_map<std::string, double>{{"time", 0.5}, {"frame", 15.0}, {"value", 0.5}},
        0.0);
    CHECK(first->scene.layers()[0].transform.opacity == doctest::Approx(expected).epsilon(0.0001));
    CHECK(repeated->scene.layers()[0].transform.opacity ==
          doctest::Approx(first->scene.layers()[0].transform.opacity));
}

TEST_CASE("render plan camera shake is random-access deterministic at each frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    auto source = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"}})});
    source["camera"] = {{"type", "perspective"}};
    source["camera_shake"] = {{"amplitude_px", 16}, {"frequency_hz", 7},
        {"decay", 0}, {"seed", 91}, {"rotation_amplitude_deg", 1.0}};
    const auto decoded = decode_render_plan(source);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto at_frame = [&](std::int64_t frame) {
        return evaluate(prepared->compiled_composition, {}, Frame{frame});
    };
    const auto first = at_frame(8);
    const auto repeated = at_frame(8);
    REQUIRE(first);
    REQUIRE(repeated);
    const auto& a = first->scene.camera_2_5d();
    const auto& b = repeated->scene.camera_2_5d();
    CHECK(a.position.x == doctest::Approx(b.position.x));
    CHECK(a.position.y == doctest::Approx(b.position.y));
    CHECK(a.rotation.z == doctest::Approx(b.rotation.z));
    const auto other_frame = at_frame(9);
    REQUIRE(other_frame);
    CHECK(other_frame->scene.camera_2_5d().position.x != doctest::Approx(a.position.x));
}

TEST_CASE("render plan applies animated effect parameters at the sampled frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    auto source = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"}})});
    source["layers"][0]["effects"] = {{{"type", "gaussian_blur"}, {"radius", 0.0}}};
    source["layers"][0]["effect_param_tracks"] = nlohmann::json::array({{
        {"effect_id", "blur.gaussian"}, {"param", "radius"},
        {"keyframes", {{{"frame", 0}, {"value", 0.0}}, {{"frame", 10}, {"value", 100.0}}}}
    }});
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition, {}, Frame{5});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers()[0].effects().size() == 1);
    const auto* blur = std::get_if<BlurParams>(&evaluated->scene.layers()[0].effects()[0].params);
    REQUIRE(blur);
    CHECK(blur->radius == doctest::Approx(50.0f));
}

TEST_CASE("render plan compiles and samples a catalog-addressed effect track") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    auto source = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"}})});
    source["layers"][0]["effects"] = nlohmann::json::array({nlohmann::json{
        {"type", "catalog"}, {"effect_id", "color.tint"},
        {"params", nlohmann::json::array({nlohmann::json{{"param", "amount"}, {"value", 0.25}}})}
    }});
    source["layers"][0]["effect_param_tracks"] = nlohmann::json::array({{
        {"effect_id", "color.tint"}, {"param", "amount"},
        {"keyframes", {{{"frame", 0}, {"value", 0.0}}, {{"frame", 10}, {"value", 1.0}}}}
    }});
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition, {}, Frame{5});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers()[0].effects().size() == 1);
    const auto* tint = std::get_if<TintParams>(&evaluated->scene.layers()[0].effects()[0].params);
    REQUIRE(tint);
    CHECK(tint->amount == doctest::Approx(0.5f));
}

TEST_CASE("render plan samples inline data-feed values at the requested frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    auto source = render_plan_test::base_plan({nlohmann::json{
        {"id", "data-fed"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"animation", {{"tracks", nlohmann::json::array({nlohmann::json{
            {"property", "opacity"},
            {"data_feed", {{"values", {0.0, 0.5, 1.0}}, {"interpolation", "linear"}}}
        }})}}}
    }});
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(*decoded, resolver);
    REQUIRE(prepared);
    const auto first = evaluate(prepared->compiled_composition, {}, Frame{1});
    const auto repeat = evaluate(prepared->compiled_composition, {}, Frame{1});
    REQUIRE(first);
    REQUIRE(repeat);
    CHECK(first->scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
    CHECK(repeat->scene.layers()[0].transform.opacity == first->scene.layers()[0].transform.opacity);
}

TEST_CASE("render plan resolves digest-bound data-feed assets before evaluation") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    const auto series_path = directory.path / "series.json";
    { std::ofstream output(series_path); output << "[0,0.25,0.75,1]"; }
    const auto digest = assets::sha256_file(series_path);
    REQUIRE(digest);
    auto source = render_plan_test::base_plan({nlohmann::json{
        {"id", "asset-fed"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"animation", {{"tracks", nlohmann::json::array({nlohmann::json{
            {"property", "opacity"},
            {"data_feed", {{"asset", "series.json"}, {"sha256", digest->hex()},
                             {"item_count", 4}, {"interpolation", "linear"}}}
        }})}}}
    }});
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto prepared = compile_render_plan(*decoded, resolver);
    if (!prepared) MESSAGE(prepared.error().message);
    REQUIRE(prepared);
    const auto evaluated = evaluate(prepared->compiled_composition, {}, Frame{1});
    REQUIRE(evaluated);
    CHECK(evaluated->scene.layers()[0].transform.opacity == doctest::Approx(0.25f));
    auto tampered = source;
    tampered["layers"][0]["animation"]["tracks"][0]["data_feed"]["sha256"] = std::string(64, '0');
    const auto tampered_plan = decode_render_plan(tampered);
    REQUIRE(tampered_plan);
    const auto rejected = compile_render_plan(*tampered_plan, resolver);
    CHECK_FALSE(rejected);
}

TEST_CASE("audio_react bakes audio before evaluation and fails clearly without FFmpeg") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    const auto wav_path = directory.path / "beat.wav";
    constexpr std::uint32_t sample_rate = 48000;
    constexpr std::uint32_t sample_count = 12000;
    const auto write_u16 = [](std::ostream& out, std::uint16_t value) {
        const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8)};
        out.write(bytes, sizeof(bytes));
    };
    const auto write_u32 = [](std::ostream& out, std::uint32_t value) {
        const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8),
            static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
        out.write(bytes, sizeof(bytes));
    };
    {
        std::ofstream out(wav_path, std::ios::binary);
        out.write("RIFF", 4); write_u32(out, 36 + sample_count * 2); out.write("WAVE", 4);
        out.write("fmt ", 4); write_u32(out, 16); write_u16(out, 1); write_u16(out, 1);
        write_u32(out, sample_rate); write_u32(out, sample_rate * 2); write_u16(out, 2); write_u16(out, 16);
        out.write("data", 4); write_u32(out, sample_count * 2);
        for (std::uint32_t i = 0; i < sample_count; ++i) {
            const auto value = static_cast<std::int16_t>(std::sin(2.0 * 3.141592653589793 * 440.0 * i / sample_rate) * 12000.0);
            write_u16(out, static_cast<std::uint16_t>(value));
        }
    }
    auto source = render_plan_test::base_plan({nlohmann::json{
        {"id", "audio-reactive"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"animation", {{"tracks", nlohmann::json::array({nlohmann::json{
            {"property", "opacity"}, {"audio_react", {{"source", "mix:0"}, {"gain", 2.0}}}
        }})}}}
    }});
    source["audio"] = nlohmann::json{{"clips", nlohmann::json::array({
        nlohmann::json{{"id", "beat"}, {"asset", "beat.wav"}}})}};
    const auto decoded = decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    assets::AssetResolver resolver;
    resolver.mount(directory.path);
    const auto prepared = compile_render_plan(*decoded, resolver);
    if (!chronon3d::render_plan::detail::native_audio_react_available()) {
        CHECK_FALSE(prepared);
        if (!prepared) CHECK(prepared.error().message.find("CHRONON3D_ENABLE_NATIVE_FFMPEG") != std::string::npos);
        return;
    }
    if (!prepared) MESSAGE(prepared.error().message);
    REQUIRE(prepared);
    assets::AssetResolver repeated_resolver;
    repeated_resolver.mount(directory.path);
    const auto repeated_prepared = compile_render_plan(*decoded, repeated_resolver);
    if (!repeated_prepared) MESSAGE(repeated_prepared.error().message);
    REQUIRE(repeated_prepared);
    CHECK(repeated_prepared->fingerprint.content_digest == prepared->fingerprint.content_digest);
    const auto first = evaluate(prepared->compiled_composition, {}, Frame{3});
    const auto second = evaluate(prepared->compiled_composition, {}, Frame{3});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->scene.layers()[0].transform.opacity > 0.45f);
    CHECK(first->scene.layers()[0].transform.opacity < 0.60f);
    CHECK(second->scene.layers()[0].transform.opacity == first->scene.layers()[0].transform.opacity);
}

TEST_CASE("render plan auto_orient derives z rotation from the position tangent") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "auto-oriented-shape";
    plan.canvas = {.width = 32, .height = 32,
                   .fps = FrameRate{30, 1}, .duration = Frame{11}};
    LayerPlan layer;
    layer.id = "path-follower";
    layer.type = LayerType::Shape;
    layer.size = {8.0f, 8.0f};
    layer.size_dimensions = 2;
    layer.auto_orient = true;
    layer.shape = ShapeContentPlan{};
    AnimationTrackPlan track;
    track.property = "position";
    track.keyframes = {{Frame{0}, {0.0f, 0.0f}}, {Frame{10}, {10.0f, 10.0f}}};
    AnimationPlan animation;
    animation.tracks.push_back(std::move(track));
    layer.animation = std::move(animation);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{5});
    REQUIRE(evaluated);
    CHECK(glm::degrees(glm::eulerAngles(evaluated->scene.layers()[0].transform.rotation).z) ==
          doctest::Approx(45.0f).epsilon(0.001f));
}

TEST_CASE("viral effect presets lower through the existing effect stack") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "glitch-preset";
    plan.canvas = {.width = 32, .height = 32,
                   .fps = FrameRate{30, 1}, .duration = Frame{2}};
    LayerPlan layer;
    layer.id = "glitched";
    layer.type = LayerType::Color;
    layer.color = {0.3f, 0.4f, 0.6f, 1.0f};
    effect_presets::glitch(layer, 0.5f, 99);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 3);
    const auto* warp = std::get_if<WaveWarpParams>(&effects[0].params);
    REQUIRE(warp);
    CHECK(warp->type == WaveWarpParams::WaveType::Square);
    CHECK(warp->direction.y == doctest::Approx(1.0f));
    const auto* split = std::get_if<ChromaticAberrationParams>(&effects[1].params);
    REQUIRE(split);
    CHECK(split->red_offset == doctest::Approx(4.0f));
    CHECK(split->blue_offset == doctest::Approx(-4.0f));
    const auto* grain = std::get_if<NoiseParams>(&effects[2].params);
    REQUIRE(grain);
    CHECK(grain->animated);
    CHECK(grain->color_mode == NoiseColorMode::RGB);
    CHECK(grain->seed == 99);

    const auto aurora = effect_presets::aurora_gradient(
        Frame{0}, Frame{120}, FrameRate{30, 1});
    REQUIRE(aurora.keyframes.size() == 4);
    CHECK(aurora.keyframes.front().frame == Frame{0});
    CHECK(aurora.keyframes.back().frame == Frame{120});
    REQUIRE(aurora.keyframes[0].value.color_stops.size() == 3);
    CHECK(aurora.keyframes[0].value.color_stops[0].color.g !=
          aurora.keyframes[1].value.color_stops[0].color.g);
    CHECK_THROWS_AS(effect_presets::aurora_gradient(
        Frame{0}, Frame{0}, FrameRate{30, 1}), std::invalid_argument);

    LayerPlan scanline_layer;
    effect_presets::rgb_split(scanline_layer, 6.0f);
    effect_presets::scanlines(scanline_layer, 0.03f, 0.02f);
    effect_presets::lens_blur(scanline_layer, 5.0f);
    REQUIRE(scanline_layer.effects.size() == 3);
    CHECK(scanline_layer.effects[0].kind == LayerPlan::EffectKindPlan::ChromaticAberration);
    CHECK(scanline_layer.effects[0].chromatic_red_offset == doctest::Approx(6.0f));
    CHECK(scanline_layer.effects[1].kind == LayerPlan::EffectKindPlan::WaveWarp);
    CHECK(scanline_layer.effects[1].wave_type == 2);
    CHECK(scanline_layer.effects[2].kind == LayerPlan::EffectKindPlan::GaussianBlur);
    CHECK(scanline_layer.effects[2].radius == doctest::Approx(5.0f));
}

TEST_CASE("render plan materializes deterministic particle shapes per frame") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "particle-layer";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{25}};
    LayerPlan layer;
    layer.id = "dust";
    layer.type = LayerType::Particles;
    ParticleEmitterParams emitter;
    emitter.count = 8;
    emitter.seed = 17;
    emitter.emitter_size = {100.0f, 60.0f};
    emitter.velocity_min = {-5.0f, -12.0f};
    emitter.velocity_max = {5.0f, -2.0f};
    emitter.lifetime_min = 2.0f;
    emitter.lifetime_max = 4.0f;
    emitter.size_min = 2.0f;
    emitter.size_max = 10.0f;
    emitter.color_ramp = graphics::GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {{0.0f, Color{1.0f, 1.0f, 1.0f, 0.0f}},
         {1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}}});
    layer.particle_emitter = emitter;
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto first = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    const auto repeated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    REQUIRE(first);
    REQUIRE(repeated);
    REQUIRE(first->scene.layers().size() == 1);
    REQUIRE(repeated->scene.layers().size() == 1);
    const auto& first_nodes = first->scene.layers()[0].nodes;
    const auto& repeated_nodes = repeated->scene.layers()[0].nodes;
    REQUIRE(first_nodes.size() == emitter.count);
    REQUIRE(repeated_nodes.size() == first_nodes.size());
    for (std::size_t index = 0; index < first_nodes.size(); ++index) {
        CHECK(first_nodes[index].shape.type() == ShapeType::Circle);
        CHECK(first_nodes[index].world_transform.position.x ==
              repeated_nodes[index].world_transform.position.x);
        CHECK(first_nodes[index].world_transform.position.y ==
              repeated_nodes[index].world_transform.position.y);
    }

    // Opaque normal-blend particles share one path node while retaining one
    // closed contour per stable particle sample.
    RenderPlan batch_plan;
    batch_plan.job_id = "particle-batch";
    batch_plan.canvas = plan.canvas;
    LayerPlan batch_layer;
    batch_layer.id = "opaque-dust";
    batch_layer.type = LayerType::Particles;
    emitter.color_ramp = {};
    batch_layer.particle_emitter = emitter;
    batch_plan.layers.push_back(std::move(batch_layer));
    const auto batch_prepared = compile_render_plan(batch_plan, resolver);
    REQUIRE(batch_prepared);
    const auto batch_frame = evaluate(
        batch_prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    REQUIRE(batch_frame);
    REQUIRE(batch_frame->scene.layers().size() == 1);
    REQUIRE(batch_frame->scene.layers()[0].nodes.size() == 1);
    const auto& batch_path = batch_frame->scene.layers()[0].nodes[0].shape.path();
    CHECK(batch_frame->scene.layers()[0].nodes[0].shape.type() == ShapeType::Path);
    CHECK(std::count_if(batch_path.commands.begin(), batch_path.commands.end(),
        [](const auto& command) { return command.type == PathCommandType::Close; }) ==
        emitter.count);
    CHECK(batch_path.commands.size() <= emitter.count * 65);
}

TEST_CASE("render plan compiles radial light into a screen-space gradient circle") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "radial-light-layer";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "warm-light";
    layer.type = LayerType::Light;
    layer.position = {160.0f, 90.0f, 0.0f};
    layer.position_dimensions = 2;
    layer.light = LightLayerPlan{
        .radius = 80.0f, .color = {1.0f, 0.5f, 0.25f, 1.0f}, .intensity = 1.25f};
    layer.blend_mode = BlendMode::Screen;
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    REQUIRE(evaluated->scene.layers()[0].nodes.size() == 1);
    CHECK(evaluated->scene.layers()[0].nodes[0].shape.type() == ShapeType::Circle);
}

TEST_CASE("render plan lowers point spot and directional lights to per-pixel scene sources") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    assets::AssetResolver resolver;
    for (const auto kind : {LightKindPlan::Point, LightKindPlan::Spot, LightKindPlan::Directional}) {
        RenderPlan plan;
        plan.job_id = "unrendered-light-kind";
        plan.canvas = {.width = 64, .height = 64,
                       .fps = FrameRate{24, 1}, .duration = Frame{1}};
        LayerPlan layer;
        layer.id = "key-light";
        layer.type = LayerType::Light;
        layer.position = {32.0f, 32.0f, 0.0f};
        layer.position_dimensions = 2;
        LightLayerPlan light;
        light.kind = kind;
        layer.light = light;
        plan.layers.push_back(std::move(layer));
        // Non-radial lights are sources: no glow geometry, one per-pixel source.
        const auto prepared = compile_render_plan(plan, resolver);
        REQUIRE(prepared);
        const auto evaluated = evaluate(
            prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
        REQUIRE(evaluated);
        CHECK(evaluated->scene.layers()[0].nodes.empty());
        REQUIRE(evaluated->scene.light_context().point_lights.size() == 1);
        // Default radius (256) is used as the height of a 2D light.
        CHECK(evaluated->scene.light_context().point_lights[0].position.z == doctest::Approx(256.0f));
    }
}

TEST_CASE("render plan 2D point light defaults its height to the light radius") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    assets::AssetResolver resolver;
    RenderPlan plan;
    plan.job_id = "point-light-height";
    plan.canvas = {.width = 128, .height = 96,
                   .fps = FrameRate{24, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "key";
    layer.type = LayerType::Light;
    layer.position = {40.0f, 30.0f, 0.0f};
    layer.position_dimensions = 2;
    LightLayerPlan light;
    light.kind = LightKindPlan::Point;
    light.radius = 90.0f;
    layer.light = light;
    plan.layers.push_back(std::move(layer));
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.light_context().point_lights.size() == 1);
    const auto& source = evaluated->scene.light_context().point_lights[0];
    CHECK(source.position.x == doctest::Approx(40.0f));
    CHECK(source.position.y == doctest::Approx(30.0f));
    CHECK(source.position.z == doctest::Approx(90.0f));
}

TEST_CASE("RenderPlan forwards per-light shadow settings to scene lighting") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "cpu-light-shadow";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{1}};
    LayerPlan caster;
    caster.id = "caster";
    caster.type = LayerType::Color;
    caster.color = {0.8f, 0.4f, 0.2f, 1.0f};
    caster.position = {80.0f, 40.0f, 20.0f};
    caster.position_dimensions = 3;
    // Casting and receiving are independent material policies. A caster that
    // opts out of receiving shadows still projects its own silhouette.
    caster.accepts_shadows = false;
    plan.layers.push_back(caster);
    LayerPlan no_shadow_caster = caster;
    no_shadow_caster.id = "shadow-opt-out";
    no_shadow_caster.position[0] = 140.0f;
    no_shadow_caster.casts_shadows = false;
    plan.layers.push_back(std::move(no_shadow_caster));
    LayerPlan light_layer;
    light_layer.id = "shadow-light";
    light_layer.type = LayerType::Light;
    light_layer.position = {20.0f, 10.0f, 100.0f};
    light_layer.position_dimensions = 3;
    LightLayerPlan light;
    light.kind = LightKindPlan::Point;
    light.cast_shadow = true;
    light.shadow_radius = 7.0f;
    light.shadow_bias = 0.5f;
    light_layer.light = light;
    plan.layers.push_back(std::move(light_layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 3);
    const auto& lights = evaluated->scene.light_context().point_lights;
    REQUIRE(lights.size() == 1);
    CHECK(lights[0].casts_shadow);
    CHECK(lights[0].shadow_radius == doctest::Approx(7.0f));
    CHECK(lights[0].shadow_bias == doctest::Approx(0.5f));
    CHECK(evaluated->scene.layers()[0].effects().empty());
    CHECK(evaluated->scene.layers()[1].effects().empty());
}

TEST_CASE("RenderPlan source lights sample position and direction with deterministic random access") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    for (const auto kind : {LightKindPlan::Point, LightKindPlan::Spot, LightKindPlan::Directional}) {
        RenderPlan plan;
        plan.canvas = {.width = 320, .height = 180,
                       .fps = FrameRate{24, 1}, .duration = Frame{24}};
        LayerPlan layer;
        layer.id = "moving-light";
        layer.type = LayerType::Light;
        layer.position = {40.0f, 30.0f, 100.0f};
        layer.position_dimensions = 3;
        layer.light = LightLayerPlan{};
        layer.light->kind = kind;
        layer.animation = AnimationPlan{{
            AnimationTrackPlan{.property = "position_x",
                .keyframes = {{Frame{0}, {0.0f}}, {Frame{20}, {80.0f}}}},
            AnimationTrackPlan{.property = "light_direction",
                .keyframes = {{Frame{0}, {0.0f, 0.0f, -1.0f}},
                              {Frame{20}, {1.0f, 0.0f, -1.0f}}}}}};
        plan.layers.push_back(layer);
        assets::AssetResolver resolver;
        const auto prepared = compile_render_plan(plan, resolver);
        REQUIRE(prepared);
        for (const int frame : {20, 0, 10, 0, 20}) {
            const auto sampled = evaluate(prepared->compiled_composition,
                CompositionEvaluateContext{}, Frame{frame});
            REQUIRE(sampled);
            const auto& lights = sampled->scene.light_context().point_lights;
            REQUIRE(lights.size() == 1);
            CHECK(lights[0].position.x == doctest::Approx(40.0f + 4.0f * frame));
            CHECK(lights[0].position.y == doctest::Approx(30.0f));
            CHECK(lights[0].position.z == doctest::Approx(100.0f));
            CHECK(lights[0].direction.x == doctest::Approx(frame / 20.0f));
            CHECK(lights[0].direction.z == doctest::Approx(-1.0f));
        }
        // A vector position track is absolute, not an underscore-axis offset.
        plan.layers[0].animation->tracks[0] = AnimationTrackPlan{
            .property = "position",
            .keyframes = {{Frame{0}, {10.0f, 20.0f}}, {Frame{20}, {30.0f, 40.0f}}}};
        const auto vector_prepared = compile_render_plan(plan, resolver);
        REQUIRE(vector_prepared);
        const auto midpoint = evaluate(vector_prepared->compiled_composition,
            CompositionEvaluateContext{}, Frame{10});
        REQUIRE(midpoint);
        const auto& position = midpoint->scene.light_context().point_lights[0].position;
        CHECK(position.x == doctest::Approx(20.0f));
        CHECK(position.y == doctest::Approx(30.0f));
        CHECK(position.z == doctest::Approx(100.0f));
    }
}

TEST_CASE("RenderPlan light direction rejects an interpolated zero vector") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{24, 1}, .duration = Frame{24}};
    LayerPlan layer;
    layer.id = "opposite-directions";
    layer.type = LayerType::Light;
    layer.light = LightLayerPlan{};
    layer.light->kind = LightKindPlan::Directional;
    layer.animation = AnimationPlan{{AnimationTrackPlan{
        .property = "light_direction",
        .keyframes = {{Frame{0}, {0.0f, 0.0f, -1.0f}},
                      {Frame{20}, {0.0f, 0.0f, 1.0f}}}}}};
    plan.layers.push_back(layer);
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto midpoint = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{10});
    REQUIRE_FALSE(midpoint);
    CHECK(midpoint.error().message.find("finite and non-zero") != std::string::npos);
    CHECK(evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0}));
    CHECK(evaluate(prepared->compiled_composition, CompositionEvaluateContext{}, Frame{20}));
}

TEST_CASE("RenderPlan light intensity and color tracks sample on requested frames") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "animated-render-plan-light";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = FrameRate{24, 1}, .duration = Frame{24}};
    LayerPlan layer;
    layer.id = "animated-light";
    layer.type = LayerType::Light;
    LightLayerPlan light;
    light.intensity = 1.0f;
    light.color = {1.0f, 1.0f, 1.0f, 1.0f};
    layer.light = light;
    layer.animation = AnimationPlan{{
        AnimationTrackPlan{.property = "light_intensity",
            .keyframes = {{Frame{0}, {0.5f}}, {Frame{23}, {1.5f}}}},
        AnimationTrackPlan{.property = "light_color",
            .keyframes = {{Frame{0}, {1.0f, 0.4f, 0.2f, 1.0f}},
                          {Frame{23}, {0.2f, 0.5f, 1.0f, 1.0f}}}}}};
    layer.blend_mode = BlendMode::Screen;
    plan.layers.push_back(std::move(layer));
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto start = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{0});
    const auto end = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{23});
    REQUIRE(start);
    REQUIRE(end);
    const auto& start_color = start->scene.layers()[0].nodes[0].color;
    const auto& end_color = end->scene.layers()[0].nodes[0].color;
    CHECK(start_color.r == doctest::Approx(0.5f));
    CHECK(start_color.g == doctest::Approx(0.2f));
    CHECK(start_color.b == doctest::Approx(0.1f));
    CHECK(end_color.r == doctest::Approx(0.3f));
    CHECK(end_color.g == doctest::Approx(0.75f));
    CHECK(end_color.b == doctest::Approx(1.5f));
}

TEST_CASE("render plan lowers fractal noise to the generate effect stack") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "procedural-background";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{24, 1}, .duration = Frame{24}};
    LayerPlan layer;
    layer.id = "background";
    layer.type = LayerType::Color;
    layer.color = {0.0f, 0.0f, 0.0f, 1.0f};
    layer.effects.push_back({
        .kind = LayerPlan::EffectKindPlan::FractalNoise,
        .seed = 7, .rgb_noise = true, .octaves = 5,
        .frequency = 0.03f, .amplitude = 0.9f,
        .evolution = 0.5f, .contrast = 1.1f, .brightness = -0.1f,
        });
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{12});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 1);
    const auto* noise = std::get_if<FractalNoiseParams>(&effects[0].params);
    REQUIRE(noise);
    CHECK(noise->seed == 7);
    CHECK(noise->octaves == 5);
    CHECK(noise->evolution == doctest::Approx(0.5f));
    CHECK(noise->color_mode == NoiseColorMode::RGB);
}

TEST_CASE("render plan lowers LightRays parameters into the effect stack") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "light-rays-plan";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{1}};
    LayerPlan layer;
    layer.id = "rays";
    layer.type = LayerType::Color;
    layer.color = {0.0f, 0.0f, 0.0f, 1.0f};
    LayerPlan::EffectPlan rays;
    rays.kind = LayerPlan::EffectKindPlan::LightRays;
    rays.light_rays_origin = {0.2f, 0.7f};
    rays.light_rays_length = 240.0f;
    rays.light_rays_density = 1.25f;
    rays.light_rays_color = {0.9f, 0.8f, 0.6f, 0.75f};
    rays.light_rays_decay = 0.85f;
    layer.effects.push_back(rays);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 1);
    const auto* decoded = std::get_if<LightRaysParams>(&effects[0].params);
    REQUIRE(decoded);
    CHECK(decoded->origin.x == doctest::Approx(0.2f));
    CHECK(decoded->origin.y == doctest::Approx(0.7f));
    CHECK(decoded->length == doctest::Approx(240.0f));
    CHECK(decoded->density == doctest::Approx(1.25f));
    CHECK(decoded->color.r == doctest::Approx(0.9f));
    CHECK(decoded->color.a == doctest::Approx(0.75f));
    CHECK(decoded->decay == doctest::Approx(0.85f));
}

TEST_CASE("render plan adjustment layers reject drawable and compositor state in direct plans") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    const auto rejected = [](const LayerPlan& invalid_layer) {
        RenderPlan plan;
        plan.job_id = "invalid-adjustment";
        plan.canvas = {.width = 16, .height = 16,
                       .fps = FrameRate{24, 1}, .duration = Frame{1}};
        LayerPlan base;
        base.id = "base";
        base.type = LayerType::Color;
        base.color = {0, 0, 0, 1};
        plan.layers.push_back(base);
        plan.layers.push_back(invalid_layer);
        assets::AssetResolver resolver;
        return compile_render_plan(plan, resolver);
    };
    LayerPlan valid;
    valid.id = "adjustment";
    valid.type = LayerType::Adjustment;
    valid.effects.push_back({.kind = LayerPlan::EffectKindPlan::Noise});
    REQUIRE(rejected(valid));

    auto with_opacity = valid;
    with_opacity.opacity = 1.0f;
    CHECK_FALSE(rejected(with_opacity));
    auto with_position = valid;
    with_position.position = {1.0f, 0.0f, 0.0f};
    with_position.position_dimensions = 2;
    CHECK_FALSE(rejected(with_position));
    auto with_compositing = valid;
    with_compositing.blend_mode = BlendMode::Normal;
    CHECK_FALSE(rejected(with_compositing));
    auto without_effect = valid;
    without_effect.effects.clear();
    CHECK_FALSE(rejected(without_effect));
}

TEST_CASE("render plan adjustment layer applies grain without drawing content") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "background-grain-adjustment";
    plan.canvas = {.width = 64, .height = 64,
                   .fps = FrameRate{24, 1}, .duration = Frame{1}};
    LayerPlan background;
    background.id = "background";
    background.type = LayerType::Color;
    background.color = {0.1f, 0.15f, 0.2f, 1.0f};
    plan.layers.push_back(std::move(background));
    LayerPlan adjustment;
    adjustment.id = "film-grain";
    adjustment.type = LayerType::Adjustment;
    adjustment.effects.push_back({
        .kind = LayerPlan::EffectKindPlan::Noise,
        .amount = 0.05f, .seed = 123, .animated = true,
        .noise_size = 1.5f});
    plan.layers.push_back(std::move(adjustment));

    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = evaluate(
        prepared->compiled_composition, CompositionEvaluateContext{}, Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 2);
    CHECK(evaluated->scene.layers()[1].kind == LayerKind::Adjustment);
    CHECK(evaluated->scene.layers()[1].nodes.empty());
    const auto& effects = evaluated->scene.layers()[1].effects();
    REQUIRE(effects.size() == 1);
    const auto* noise = std::get_if<NoiseParams>(&effects[0].params);
    REQUIRE(noise);
    CHECK(noise->seed == 123);
    CHECK(noise->animated);
    CHECK(noise->size == doctest::Approx(1.5f));
}

TEST_CASE("render plan prepares and lowers content-addressed 3D LUT assets") {
    FingerprintTempDir temp;
    std::filesystem::create_directories(temp.path / "looks");
    {
        std::ofstream cube(temp.path / "looks/invert.cube");
        cube << "LUT_3D_SIZE 2\n"
             << "1 1 1\n0 1 1\n1 0 1\n0 0 1\n"
             << "1 1 0\n0 1 0\n1 0 0\n0 0 0\n";
    }
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "lut-plan";
    plan.canvas = {.width = 16, .height = 16,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{1}};
    chronon3d::render_plan::LayerPlan color;
    color.id = "graded";
    color.type = chronon3d::render_plan::LayerType::Color;
    color.color = {0.2f, 0.3f, 0.4f, 1.0f};
    color.effects.push_back({.kind = chronon3d::render_plan::LayerPlan::EffectKindPlan::Lut3D,
                             .asset = "looks/invert.cube", .amount = 1.0f});
    plan.layers.push_back(std::move(color));
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(temp.path);
    const auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    REQUIRE(prepared->compiled_composition.asset_manifest);
    CHECK(prepared->compiled_composition.asset_manifest->assets().size() == 1);
    const auto evaluated = chronon3d::evaluate(
        prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    CHECK(evaluated->scene.layers()[0].effects().size() == 1);
}

TEST_CASE("render plan lowers luma/chroma key parameters into the effect stack") {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "keying-plan";
    plan.canvas = {.width = 16, .height = 16,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{1}};
    chronon3d::render_plan::LayerPlan color;
    color.id = "keyed";
    color.type = chronon3d::render_plan::LayerType::Color;
    color.color = {0.2f, 0.7f, 0.1f, 1.0f};
    color.effects.push_back({
        .kind = chronon3d::render_plan::LayerPlan::EffectKindPlan::Keying,
        .keying_mode = chronon3d::render_plan::LayerPlan::KeyingModePlan::Chroma,
        .key_color = {0.0f, 0.75f, 0.0f}, .threshold = 0.18f,
        .softness = 0.12f, .spill = 0.6f});
    plan.layers.push_back(std::move(color));
    chronon3d::assets::AssetResolver resolver;
    const auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    const auto evaluated = chronon3d::evaluate(
        prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{}, chronon3d::Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated->scene.layers().size() == 1);
    const auto& effects = evaluated->scene.layers()[0].effects();
    REQUIRE(effects.size() == 1);
    const auto* keying = std::get_if<chronon3d::KeyingParams>(&effects[0].params);
    REQUIRE(keying);
    CHECK(keying->mode == chronon3d::KeyingMode::Chroma);
    CHECK(keying->threshold == doctest::Approx(0.18f));
    CHECK(keying->softness == doctest::Approx(0.12f));
    CHECK(keying->spill == doctest::Approx(0.6f));
}

TEST_CASE("compiled composition preserves fractional evaluation time") {
    double observed_frame = -1.0;
    chronon3d::CompositionDefinition definition;
    definition.composition = {
        .name = "fractional-evaluation",
        .width = 320,
        .height = 180,
        .frame_rate = {24, 1},
        .duration = chronon3d::Frame{10}};
    definition.scene = [&observed_frame](const chronon3d::FrameContext& context) {
        observed_frame = context.effective_frame();
        return chronon3d::Scene{};
    };

    // Canonical path: the deprecated compile_composition(CompositionDefinition)
    // overload was removed — project onto a Composition first.
    const chronon3d::Composition definition_composition(
        definition.composition, definition.scene,
        definition.scene_content_fingerprint);
    const auto compiled =
        chronon3d::compile_composition(definition_composition, {});
    REQUIRE(compiled);

    const auto base_context = chronon3d::make_frame_context({
        .global_time = chronon3d::SampleTime::from_frame_int(
            chronon3d::Frame{0}, {24, 1}),
        .duration = chronon3d::Frame{10},
        .width = 320,
        .height = 180});
    const auto evaluated = chronon3d::evaluate(
        compiled.value(),
        chronon3d::CompositionEvaluateContext{.frame_context = base_context},
        chronon3d::SampleTime::from_frame(2.5, {24, 1}));

    REQUIRE(evaluated);
    CHECK(observed_frame == doctest::Approx(2.5));
}

TEST_CASE("prepared fingerprint is stable across asset roots and output metadata") {
    FingerprintTempDir temp;
    const auto root_a = temp.path / "root_a";
    const auto root_b = temp.path / "root_b";
    write_asset(root_a, "same bytes");
    write_asset(root_b, "same bytes");

    auto plan_a = image_plan();
    auto plan_b = plan_a;
    plan_b.job_id = "different-job-id";
    plan_b.output.path = "/different/machine/output.png";
    chronon3d::assets::AssetResolver resolver_a;
    chronon3d::assets::AssetResolver resolver_b;
    resolver_a.mount(root_a);
    resolver_b.mount(root_b);

    const auto first = chronon3d::render_plan::compile_render_plan(plan_a, resolver_a);
    const auto second = chronon3d::render_plan::compile_render_plan(plan_b, resolver_b);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->fingerprint.content_digest == second->fingerprint.content_digest);
    CHECK(first->fingerprint.request_digest == second->fingerprint.request_digest);
}

TEST_CASE("prepared fingerprint changes when asset bytes change") {
    FingerprintTempDir temp;
    const auto root = temp.path / "root";
    write_asset(root, "original bytes");
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(root);
    const auto original = chronon3d::render_plan::compile_render_plan(
        image_plan(), resolver);
    REQUIRE(original);

    write_asset(root, "modified bytes with different size");
    const auto modified = chronon3d::render_plan::compile_render_plan(
        image_plan(), resolver);
    REQUIRE(modified);
    CHECK(original->fingerprint.content_digest != modified->fingerprint.content_digest);
    CHECK(original->fingerprint.request_digest != modified->fingerprint.request_digest);
}

// ── FP-1: one job identity, two entry points ────────────────────────────────
//
// `compiler::compile` (the canonical compiler) and `compile_render_plan` (the
// plan/prepared path) must derive the SAME content/request digest for the same
// plan + assets + options. If they could differ, a program/binary cache keyed
// on the digest would be able to hit a program compiled for a non-equivalent
// job — a correctness bug, not a performance one. Before the FP-1 fix these
// were two independent implementations of the same hash sequence, kept in sync
// only by review.

TEST_CASE("the compiler path and the plan path agree on the job fingerprint") {
    FingerprintTempDir temp;
    const auto root = temp.path / "fp1_root";
    write_asset(root, "same bytes");

    auto plan = image_plan();
    plan.output.path = "fp1-job.mp4";
    plan.output.codec = chronon3d::render_plan::VideoCodec::H264;
    plan.output.rate_control = chronon3d::render_plan::RateControlMode::Bitrate;
    plan.output.bitrate = 2'000'000;

    chronon3d::render_plan::RenderPlanFingerprintOptions options;
    options.render_settings.deterministic = true;
    options.render_settings.antialiasing_samples = 2;

    chronon3d::assets::AssetResolver compiler_resolver;
    compiler_resolver.mount(root);
    const auto compiled = chronon3d::compiler::compile(
        plan, compiler_resolver,
        chronon3d::compiler::CompileOptions{.fingerprint = options});
    REQUIRE(compiled);
    const auto& program = compiled.value();
    // One program, one identity: the metadata view and the fingerprint view of
    // the same digests must not drift apart either.
    CHECK(program.fingerprint_data().content_digest ==
          program.metadata().content_digest);
    CHECK(program.fingerprint_data().request_digest ==
          program.metadata().request_digest);

    chronon3d::assets::AssetResolver plan_resolver;
    plan_resolver.mount(root);
    const auto prepared =
        chronon3d::render_plan::compile_render_plan(plan, plan_resolver, options);
    REQUIRE(prepared);

    CHECK(prepared->fingerprint.content_digest ==
          program.fingerprint_data().content_digest);
    CHECK(prepared->fingerprint.request_digest ==
          program.fingerprint_data().request_digest);
}

TEST_CASE("an unset render-settings dimension inherits the canvas on both paths") {
    FingerprintTempDir temp;
    const auto root = temp.path / "fp1_canvas_root";
    write_asset(root, "same bytes");

    const auto plan = image_plan(); // canvas 320x180
    chronon3d::render_plan::RenderPlanFingerprintOptions unset;
    unset.render_settings.width = 0;
    unset.render_settings.height = 0;

    auto inherited = unset;
    inherited.render_settings.width = plan.canvas.width;
    inherited.render_settings.height = plan.canvas.height;

    chronon3d::assets::AssetResolver resolver_a;
    resolver_a.mount(root);
    const auto inherited_prepared = chronon3d::render_plan::compile_render_plan(
        plan, resolver_a, inherited);
    REQUIRE(inherited_prepared);

    chronon3d::assets::AssetResolver resolver_b;
    resolver_b.mount(root);
    const auto unset_prepared = chronon3d::render_plan::compile_render_plan(
        plan, resolver_b, unset);
    REQUIRE(unset_prepared);

    CHECK(unset_prepared->fingerprint.content_digest ==
          inherited_prepared->fingerprint.content_digest);
    CHECK(unset_prepared->fingerprint.request_digest ==
          inherited_prepared->fingerprint.request_digest);

    // The same rule has to hold on the compiler path, which is exactly where
    // the wording of the default used to live alone.
    chronon3d::assets::AssetResolver resolver_c;
    resolver_c.mount(root);
    const auto compiled = chronon3d::compiler::compile(
        plan, resolver_c,
        chronon3d::compiler::CompileOptions{.fingerprint = unset});
    REQUIRE(compiled);
    CHECK(compiled.value().fingerprint_data().content_digest ==
          unset_prepared->fingerprint.content_digest);
    CHECK(compiled.value().fingerprint_data().request_digest ==
          unset_prepared->fingerprint.request_digest);
}

// ── Subtitle style parity (resolve_subtitle_style) ──────────────────────────
//
// The SubtitleTrack compiler consumes layer.style with EXACTLY the semantics
// of the text materializer: shared parse_hex_color (color_utils.hpp), same
// shadow defaults (opacity 1.0 / blur 0.0 / offset (0,0) when absent), same
// "absent → keep defaults" contract. These tests lock that parity.
//
// P0-2 (TICKET-TEXT-QUALITY-V2): the shared parser is the single
// authoring/lowering boundary and emits Chronon's LINEAR working domain. A
// CSS hex literal is sRGB, so it is converted exactly once here (see the
// COLOR DOMAIN note in color_utils.hpp). No text backend re-linearizes.

namespace {

void check_color(const chronon3d::Color& got,
                 float r, float g, float b, float a) {
    CHECK(got.r == doctest::Approx(r).epsilon(0.0001f));
    CHECK(got.g == doctest::Approx(g).epsilon(0.0001f));
    CHECK(got.b == doctest::Approx(b).epsilon(0.0001f));
    CHECK(got.a == doctest::Approx(a).epsilon(0.0001f));
}

} // namespace

TEST_CASE("resolve_subtitle_style lowers fill with text-materializer hex semantics") {
    chronon3d::render_plan::LayerStylePlan style;
    style.fill = "#FFC107";

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.fill.has_value());
    // #FFC107 is CSS sRGB; the lowered Color lives in the linear working
    // domain, so the expectation is the canonical sRGB→linear projection.
    const chronon3d::Color expected =
        chronon3d::Color{1.0f, 193.0f / 255.0f, 7.0f / 255.0f, 1.0f}.to_linear();
    check_color(*resolved.fill, expected.r, expected.g, expected.b, expected.a);
    // Pin one explicit value so a silent change to the conversion is caught
    // even if to_linear() itself drifts: linear(#C1) = 0.53326.
    CHECK(resolved.fill->g == doctest::Approx(0.53326f).epsilon(0.0002f));
    // ... and the encoded channel must NOT survive to the consumer.
    CHECK(resolved.fill->g != doctest::Approx(193.0f / 255.0f).epsilon(0.0001f));
    CHECK_FALSE(resolved.shadow.has_value());
    CHECK_FALSE(resolved.font_size.has_value());
}

// P0-2 gate: mid-gray is the smallest fixture that exposes an sRGB/linear
// mix-up on BOTH text authoring paths.  CSS #808080 is sRGB 0.501961; the
// linear working-domain value is 0.215861.  If a future change re-encodes the
// boundary, the two are easy to confuse and this test fails loudly.
TEST_CASE("parse_hex_color lowers CSS sRGB hex to the linear working domain") {
    const auto gray = chronon3d::render_plan::parse_hex_color("#808080");
    REQUIRE(gray.has_value());
    check_color(*gray, 0.2158605f, 0.2158605f, 0.2158605f, 1.0f);
    CHECK(gray->r != doctest::Approx(128.0f / 255.0f).epsilon(0.0001f));

    // Alpha is a coverage factor, not a channel: it survives untouched.
    const auto half = chronon3d::render_plan::parse_hex_color("#808080", 0.5f);
    REQUIRE(half.has_value());
    check_color(*half, 0.2158605f, 0.2158605f, 0.2158605f, 0.5f);

    // Fixed points of the conversion stay exact and keep every existing
    // white-fill / black-shadow production plan byte-stable.
    const auto white = chronon3d::render_plan::parse_hex_color("#FFFFFF");
    const auto black = chronon3d::render_plan::parse_hex_color("#000000");
    REQUIRE(white.has_value());
    REQUIRE(black.has_value());
    check_color(*white, 1.0f, 1.0f, 1.0f, 1.0f);
    check_color(*black, 0.0f, 0.0f, 0.0f, 1.0f);
}

TEST_CASE("resolve_subtitle_style lowers mid-gray to the linear domain") {
    chronon3d::render_plan::LayerStylePlan style;
    style.fill = "#808080";

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.fill.has_value());
    check_color(*resolved.fill, 0.2158605f, 0.2158605f, 0.2158605f, 1.0f);
}

TEST_CASE("resolve_subtitle_style keeps defaults when fill is unusable") {
    chronon3d::render_plan::LayerStylePlan style;
    style.fill = "not-a-hex";  // same guard the text materializer applies

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    CHECK_FALSE(resolved.fill.has_value());
}

TEST_CASE("resolve_subtitle_style lowers shadow with materializer defaults") {
    chronon3d::render_plan::LayerStylePlan style;
    style.fill = "#FFFFFF";
    chronon3d::render_plan::ShadowStyle shadow;
    shadow.color = "#000000";
    shadow.opacity = 0.6f;
    shadow.blur = 14.0f;
    shadow.offset = {0.0f, 8.0f};
    shadow.offset_dimensions = 2;
    style.shadow = shadow;

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.shadow.has_value());
    const auto& got = *resolved.shadow;
    CHECK(got.enabled);
    check_color(got.color, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(got.opacity == doctest::Approx(0.6f));
    CHECK(got.blur == doctest::Approx(14.0f));
    CHECK(got.offset.x == doctest::Approx(0.0f));
    CHECK(got.offset.y == doctest::Approx(8.0f));
}

TEST_CASE("resolve_subtitle_style shadow defaults match the text materializer") {
    chronon3d::render_plan::LayerStylePlan style;
    chronon3d::render_plan::ShadowStyle shadow;
    shadow.color = "#000000";  // no opacity / blur / offset declared
    style.shadow = shadow;

    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.shadow.has_value());
    const auto& got = *resolved.shadow;
    CHECK(got.enabled);
    CHECK(got.opacity == doctest::Approx(1.0f));  // absent → 1.0
    CHECK(got.blur == doctest::Approx(0.0f));     // absent → 0.0
    CHECK(got.offset.x == doctest::Approx(0.0f));
    CHECK(got.offset.y == doctest::Approx(0.0f));
}

TEST_CASE("resolve_subtitle_style lowers font_size when positive and absent otherwise") {
    chronon3d::render_plan::LayerStylePlan style;
    style.font_size = 54.0f;
    const auto resolved = chronon3d::render_plan::resolve_subtitle_style(style);
    REQUIRE(resolved.font_size.has_value());
    CHECK(*resolved.font_size == doctest::Approx(54.0f));

    chronon3d::render_plan::LayerStylePlan empty_style;
    const auto empty = chronon3d::render_plan::resolve_subtitle_style(empty_style);
    CHECK_FALSE(empty.font_size.has_value());
    CHECK_FALSE(empty.fill.has_value());
    CHECK_FALSE(empty.shadow.has_value());
}

TEST_CASE("prepared fingerprint includes schema, engine, and render settings") {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "fingerprint-settings";
    plan.canvas = {.width = 320, .height = 180,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{3}};
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::RenderPlanFingerprintOptions base;
    const auto baseline = chronon3d::render_plan::compile_render_plan(
        plan, resolver, base);
    REQUIRE(baseline);

    auto schema_changed = base;
    schema_changed.schema_version = "chronon.render-plan.v3";
    auto engine_changed = base;
    engine_changed.engine_compatibility_version = "chronon3d.engine.v2";
    auto settings_changed = base;
    settings_changed.render_settings.antialiasing_samples = 4;
    settings_changed.render_settings.deterministic = true;

    const auto schema = chronon3d::render_plan::compile_render_plan(
        plan, resolver, schema_changed);
    const auto engine = chronon3d::render_plan::compile_render_plan(
        plan, resolver, engine_changed);
    const auto settings = chronon3d::render_plan::compile_render_plan(
        plan, resolver, settings_changed);
    REQUIRE(schema);
    REQUIRE(engine);
    REQUIRE(settings);
    CHECK(baseline->fingerprint.content_digest != schema->fingerprint.content_digest);
    CHECK(baseline->fingerprint.content_digest != engine->fingerprint.content_digest);
    CHECK(baseline->fingerprint.content_digest != settings->fingerprint.content_digest);
}

TEST_CASE("render_plan_compiler: animation tracks map to animated transforms") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "anim_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 0.0f, 0.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan track;
    track.property = "opacity";
    track.easing = "bezier";
    track.bezier_curve = std::array<float, 4>{0.16f, 1.0f, 0.3f, 1.0f};
    track.keyframes.push_back({chronon3d::Frame{0}, {0.0f}});
    track.keyframes.push_back({chronon3d::Frame{15}, {1.0f}});
    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(track)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_anim";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto evaluated = chronon3d::evaluate(
        prepared->compiled_composition,
        chronon3d::CompositionEvaluateContext{},
        chronon3d::Frame{0});
    REQUIRE(evaluated);
    REQUIRE(evaluated.value().scene.layers().size() == 1u);
    CHECK(evaluated.value().scene.layers().front().anim_transform.opacity.is_animated());
}

TEST_CASE("render_plan_compiler: scalar temporal handles and AutoBezier lower to AnimatedValue") {
    using namespace chronon3d;
    assets::AssetResolver resolver;
    render_plan::LayerPlan layer;
    layer.id = "scalar-timing";
    layer.type = render_plan::LayerType::Color;
    layer.color = {1, 1, 1, 1};
    render_plan::AnimationTrackPlan track;
    track.property = "opacity";
    render_plan::AnimationKeyframePlan start;
    start.frame = Frame{0};
    start.value = {0.0f};
    start.interpolation = "auto_bezier";
    render_plan::AnimationKeyframePlan middle;
    middle.frame = Frame{10};
    middle.value = {0.5f};
    middle.roving = true;
    render_plan::AnimationKeyframePlan end;
    end.frame = Frame{20};
    end.value = {1.0f};
    track.keyframes = {start, middle, end};
    layer.animation = render_plan::AnimationPlan{.tracks = {track}};
    render_plan::RenderPlan plan;
    plan.job_id = "scalar-timing";
    plan.canvas = {.width = 320, .height = 180, .fps = FrameRate{30, 1},
                   .duration = Frame{20}};
    plan.layers.push_back(std::move(layer));
    auto prepared = render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto evaluated = evaluate(prepared->compiled_composition,
        CompositionEvaluateContext{}, Frame{5});
    REQUIRE(evaluated);
    const auto& opacity = evaluated.value().scene.layers().front().anim_transform.opacity;
    REQUIRE(opacity.keyframes().size() == 3);
    CHECK(opacity.keyframes()[0].interp == InterpMode::AutoBezier);
    CHECK(opacity.keyframes()[1].roving);
    CHECK(opacity.keyframes()[1].frame == Frame{10});
}

TEST_CASE("render_plan_compiler: position_x and position_y merge correctly (diagonal drift)") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "drift_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan track_x;
    track_x.property = "position_x";
    track_x.easing = "linear";
    track_x.keyframes.push_back({chronon3d::Frame{0}, {0.0f}});
    track_x.keyframes.push_back({chronon3d::Frame{30}, {100.0f}});

    chronon3d::render_plan::AnimationTrackPlan track_y;
    track_y.property = "position_y";
    track_y.easing = "linear";
    track_y.keyframes.push_back({chronon3d::Frame{0}, {0.0f}});
    track_y.keyframes.push_back({chronon3d::Frame{30}, {200.0f}});

    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(track_x), std::move(track_y)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_drift";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);

    auto eval0 = chronon3d::evaluate(prepared->compiled_composition, {}, chronon3d::Frame{0});
    REQUIRE(eval0);
    CHECK(eval0.value().scene.layers().front().anim_transform.position.is_animated());
}

TEST_CASE("render_plan_compiler: rejects mixing position with component tracks") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "bad_mix_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan track_full;
    track_full.property = "position";
    track_full.keyframes.push_back({chronon3d::Frame{0}, {0.0f, 0.0f, 0.0f}});

    chronon3d::render_plan::AnimationTrackPlan track_x;
    track_x.property = "position_x";
    track_x.keyframes.push_back({chronon3d::Frame{0}, {50.0f}});

    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(track_full), std::move(track_x)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_bad_mix";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto eval = chronon3d::evaluate(prepared->compiled_composition, {}, chronon3d::Frame{0});
    CHECK(!eval);
    CHECK(eval.error().message.find("mixes 'position' with component tracks") != std::string::npos);
}

TEST_CASE("render_plan_compiler: rejects duplicate component tracks") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "dup_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan track_x1;
    track_x1.property = "position_x";
    track_x1.keyframes.push_back({chronon3d::Frame{0}, {10.0f}});

    chronon3d::render_plan::AnimationTrackPlan track_x2;
    track_x2.property = "position_x";
    track_x2.keyframes.push_back({chronon3d::Frame{0}, {20.0f}});

    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(track_x1), std::move(track_x2)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_dup";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto eval = chronon3d::evaluate(prepared->compiled_composition, {}, chronon3d::Frame{0});
    CHECK(!eval);
    CHECK(eval.error().message.find("duplicate position_x tracks") != std::string::npos);
}

TEST_CASE("render_plan_compiler: rotation_x and rotation_y merge correctly (dual gimbal)") {
    chronon3d::assets::AssetResolver resolver;

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "gimbal_layer";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.color = {1.0f, 1.0f, 1.0f, 1.0f};

    chronon3d::render_plan::AnimationTrackPlan rot_x;
    rot_x.property = "rotation_x";
    rot_x.keyframes.push_back({chronon3d::Frame{0}, {10.0f}});
    rot_x.keyframes.push_back({chronon3d::Frame{30}, {20.0f}});

    chronon3d::render_plan::AnimationTrackPlan rot_y;
    rot_y.property = "rotation_y";
    rot_y.keyframes.push_back({chronon3d::Frame{0}, {-5.0f}});
    rot_y.keyframes.push_back({chronon3d::Frame{30}, {5.0f}});

    layer.animation = chronon3d::render_plan::AnimationPlan{.tracks = {std::move(rot_x), std::move(rot_y)}};

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "test_gimbal";
    plan.canvas = {.width = 1280, .height = 720,
                   .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    plan.layers.push_back(std::move(layer));

    auto prepared = chronon3d::render_plan::compile_render_plan(plan, resolver);
    REQUIRE(prepared);
    auto eval0 = chronon3d::evaluate(prepared->compiled_composition, {}, chronon3d::Frame{0});
    REQUIRE(eval0);
    CHECK(eval0.value().scene.layers().front().anim_transform.rotation_euler.is_animated());
}

TEST_CASE("render plan flow layout reaches the runtime scene configuration") {
    using namespace chronon3d;
    assets::AssetResolver resolver;
    render_plan::RenderPlan plan;
    plan.job_id="flow-runtime";
    plan.canvas={.width=600,.height=300,.fps=FrameRate{30,1},.duration=Frame{10}};
    for (int i=0;i<3;++i) {
        render_plan::LayerPlan layer;
        layer.id="flow-"+std::to_string(i); layer.type=render_plan::LayerType::Color;
        layer.color={1,1,1,1};
        layer.flow_layout=render_plan::LayerPlan::FlowLayoutPlan{
            .group_id="cards",.axis="row",.align="center",.justify="center",
            .gap=12,.wrap=false,.cell_size={100,80}};
        plan.layers.push_back(std::move(layer));
    }
    auto prepared=render_plan::compile_render_plan(plan,resolver);
    REQUIRE(prepared);
    auto evaluated=evaluate(prepared->compiled_composition,{},Frame{0});
    REQUIRE(evaluated);
    const auto& layers=evaluated.value().scene.layers();
    REQUIRE(layers.size()==3);
    CHECK(layers[0].layout.flow.has_value());
    CHECK(layers[1].layout.flow->group_id=="cards");
    CHECK(layers[0].layout.flow->gap == doctest::Approx(12.0f));
    CHECK(layers[0].layout.flow->cell_size.x == doctest::Approx(100.0f));
    CHECK(layers[0].layout.flow->cell_size.y == doctest::Approx(80.0f));

    auto changed=plan;
    changed.layers[0].flow_layout->gap=20;
    auto changed_prepared=compile_render_plan(changed,resolver);
    REQUIRE(changed_prepared);
    CHECK(changed_prepared->fingerprint.content_digest != prepared->fingerprint.content_digest);
}
