#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include "../../src/render_plan/render_plan_compiler_detail.hpp"
#include <array>
#include <chronon3d/render_plan/subtitle_style.hpp>
#include <chronon3d/scene/model/core/transition_catalog_ids.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/text/text_color_packing.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#if defined(CHRONON_TEST_ENABLE_SVG)
#include <chronon3d/render_plan/render_plan.hpp>
#endif
#include <chronon3d/timeline/compile_evaluate.hpp>
#include "test_render_plan_shape_helpers.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
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
    CHECK(scene.layers()[0].transition_in.transition_id == "slide");
    CHECK(scene.layers()[0].transition_in.duration == doctest::Approx(0.2));
    CHECK(scene.layers()[0].transition_in.delay == doctest::Approx(1.0 / 30.0));
    const auto transition_hash = chronon3d::graph::hash_layer_transition_spec(
        scene.layers()[0].transition_in);
    auto changed_spec = scene.layers()[0].transition_in;
    changed_spec.parameters = chronon3d::SlideParams{1.5f};
    CHECK(chronon3d::graph::hash_layer_transition_spec(changed_spec) != transition_hash);
}

TEST_CASE("render plan lowers a bounded static Lottie path through the resolver") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "shapes");
    {
        std::ofstream file(directory.path / "shapes/mark.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,
          "ks":{"a":{"a":0,"k":[0,0,0]},"p":{"a":0,"k":[0,0,0]},
            "s":{"a":0,"k":[100,100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100}},
          "shapes":[{"ty":"sh","ks":{"a":0,"k":{"c":true,
            "v":[[0,0],[20,0],[20,20]],"i":[[0,0],[0,0],[0,0]],
            "o":[[0,0],[0,0],[0,0]]}}},
            {"ty":"fl","r":1,"c":{"a":0,"k":[1,0,0,1]}}]}]})JSON";
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

    auto unsupported = *decoded;
    // The decoder is allowed to retain the reference; the compiler rejects
    // unsupported expressions explicitly instead of silently dropping them.
    {
        std::ofstream file(directory.path / "shapes/mark.json");
        file << R"JSON({"v":"5.7.4","layers":[{"ty":4,"ddd":0,"exp":"x"}]})JSON";
    }
    CHECK_FALSE(compile_render_plan(unsupported, resolver));
}

#if defined(CHRONON_TEST_ENABLE_SVG)
TEST_CASE("render plan imports a resolver-backed SVG before composition compile") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    FingerprintTempDir directory;
    std::filesystem::create_directories(directory.path / "art");
    {
        std::ofstream svg(directory.path / "art/vector.svg");
        svg << R"(<svg><path d="M 0 0 L 20 0 L 20 20 Z" fill="#ff0000" stroke="none"/></svg>)";
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

    auto missing = *decoded;
    missing.layers[0].shape->svg_asset = "art/not-found.svg";
    CHECK_FALSE(compile_render_plan(missing, resolver));
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
        solid, 2.0, Color{0.0f, 0.0f, 1.0f, 1.0f});
    REQUIRE(before.has_value());
    CHECK(before->b == doctest::Approx(1.0f));
    const auto held = chronon3d::render_plan::detail::fill_color_at(
        solid, 7.0, Color{0.0f, 0.0f, 1.0f, 1.0f});
    REQUIRE(held.has_value());
    CHECK(held->r == doctest::Approx(1.0f));
    CHECK(held->g == doctest::Approx(0.0f));
    const auto last = chronon3d::render_plan::detail::fill_color_at(
        solid, 10.0, Color{0.0f, 0.0f, 1.0f, 1.0f});
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
