#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>

#include "test_render_plan_shape_helpers.hpp"

#include <utility>
#include <limits>
#include <type_traits>
#include <nlohmann/json.hpp>

TEST_CASE("render plan decoder constructs typed V2 plan") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 1920}, {"height", 1080}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 60}}},
        {"layers", {{{"id", "title"}, {"type", "text"},
                      {"text", "Hello"},
                      {"style", {{"font", "fonts/roboto.ttf"}, {"font_size", 24.0}, {"fill", "#FFFFFF"}}},
                      {"start_frame", 3}}}},
        {"output", {{"path", "out.mp4"}, {"format", "mp4"},
                     {"codec", "h264"}}},
        {"budget", {{"max_temporal_pixels", 4096}}}};

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    CHECK(decoded->canvas.width == 1920);
    CHECK(decoded->layers.size() == 1);
    CHECK(decoded->layers[0].type == chronon3d::render_plan::LayerType::Text);
    CHECK(decoded->layers[0].start_frame->integral() == 3);
    CHECK(decoded->output.codec == chronon3d::render_plan::VideoCodec::H264);
    CHECK(decoded->budget.max_temporal_pixels == 4096);
}

TEST_CASE("render plan decoder decodes concrete primitive layers") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 640}, {"height", 360}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 30}}},
        {"layers", nlohmann::json::array({
            {{"id", "bg_color"}, {"type", "color"}, {"color", {0.1, 0.2, 0.3, 1.0}}, {"screen_space", true}},
            {{"id", "img_01"}, {"type", "image"}, {"asset", "portrait.png"}, {"size", {100.0, 100.0}}, {"enable_3d", true}},
            {{"id", "vid_01"}, {"type", "video"}, {"source", "clips/intro.mp4"}},
            {{"id", "txt_01"}, {"type", "text"}, {"text", "Hello"},
             {"style", {{"font", "fonts/roboto.ttf"}, {"font_size", 32.0}, {"fill", "#FFFFFF"}}}}
        })},
        {"output", {{"path", "out.mp4"}}}};

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded.has_value());
    CHECK(decoded->layers.size() == 4);
    CHECK(decoded->layers[0].type == chronon3d::render_plan::LayerType::Color);
    CHECK(decoded->layers[0].screen_space);
    CHECK(decoded->layers[1].type == chronon3d::render_plan::LayerType::Image);
    CHECK(decoded->layers[1].enable_3d);
    CHECK(decoded->layers[2].type == chronon3d::render_plan::LayerType::Video);
    CHECK(decoded->layers[3].type == chronon3d::render_plan::LayerType::Text);
}

namespace {

nlohmann::json v3_text_plan() {
    return nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"txt","type":"text","text":"Hello",
        "style":{"font":"fonts/roboto.ttf","font_size":32.0,"fill":"#FFFFFF"}}],
      "output":{"path":"out.mp4"}})JSON");
}

} // namespace

TEST_CASE("render plan V3 decodes per-glyph rotation and fill_color animator tracks") {
    auto source = v3_text_plan();
    source["layers"][0]["text_animators"] = nlohmann::json::parse(R"JSON([
        {
            "id": "karaoke",
            "selectors": [
                {
                    "unit": "word", "shape": "square", "order": "forward",
                    "start": {"keyframes": [{"frame": 0, "value": 0.0}]},
                    "end": {"keyframes": [{"frame": 0, "value": 33.33}]}
                }
            ],
            "properties": [
                {
                    "property": "fill_color",
                    "keyframes": [
                        {"frame": 0, "value": [0.9, 0.1, 0.1, 1.0]},
                        {"frame": 12, "value": [1.0, 0.8, 0.0, 1.0]}
                    ]
                },
                {
                    "property": "rotation",
                    "keyframes": [
                        {"frame": 0, "value": 0.0},
                        {"frame": 30, "value": 45.0}
                    ]
                }
            ]
        }
    ])JSON");

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->layers[0].text_animators.size() == 1);
    const auto& properties = decoded->layers[0].text_animators[0].properties;
    REQUIRE(properties.size() == 2);
    CHECK(properties[0].property == "fill_color");
    REQUIRE(properties[0].keyframes.size() == 2);
    CHECK(properties[0].keyframes[0].value ==
          std::vector<float>{0.9f, 0.1f, 0.1f, 1.0f});
    CHECK(properties[1].property == "rotation");
    CHECK(properties[1].keyframes.back().value == std::vector<float>{45.0f});
}

TEST_CASE("render plan V2 keeps the frozen animator property enum") {
    auto source = v3_text_plan();
    source["schema"] = "chronon.render-plan.v2";
    source["version"] = 2;
    source["layers"][0]["text_animators"] = nlohmann::json::parse(R"JSON([
        {
            "id": "spin",
            "selectors": [{"unit": "glyph"}],
            "properties": [{"property": "rotation", "keyframes": [{"frame": 0, "value": 45.0}]}]
        }
    ])JSON");

    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan V3 rejects malformed rotation and fill_color tracks") {
    // fill_color requires RGBA quadruples.
    auto fill_three = v3_text_plan();
    fill_three["layers"][0]["text_animators"] = nlohmann::json::parse(R"JSON([
        {
            "id": "bad_fill",
            "selectors": [{"unit": "glyph"}],
            "properties": [{"property": "fill_color", "keyframes": [{"frame": 0, "value": [1.0, 0.0, 0.0]}]}]
        }
    ])JSON");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(fill_three).has_value());

    // rotation accepts scalars or XYZ triples, never pairs.
    auto rotation_pair = v3_text_plan();
    rotation_pair["layers"][0]["text_animators"] = nlohmann::json::parse(R"JSON([
        {
            "id": "bad_rotation",
            "selectors": [{"unit": "glyph"}],
            "properties": [{"property": "rotation", "keyframes": [{"frame": 0, "value": [10.0, 20.0]}]}]
        }
    ])JSON");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(rotation_pair).has_value());

    // fill_color stays inside the closed 0..1 channel vocabulary.
    auto fill_hot = v3_text_plan();
    fill_hot["layers"][0]["text_animators"] = nlohmann::json::parse(R"JSON([
        {
            "id": "hot_fill",
            "selectors": [{"unit": "glyph"}],
            "properties": [{"property": "fill_color", "keyframes": [{"frame": 0, "value": [2.0, 0.0, 0.0, 1.0]}]}]
        }
    ])JSON");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(fill_hot).has_value());
}

TEST_CASE("render plan rejects unknown text animator properties") {
    auto source = v3_text_plan();
    source["layers"][0]["text_animators"] = nlohmann::json::parse(R"JSON([
        {
            "id": "unknown",
            "selectors": [{"unit": "glyph"}],
            "properties": [{"property": "skew", "keyframes": [{"frame": 0, "value": 5.0}]}]
        }
    ])JSON");

    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan decoder accepts bounded repeater operators and rejects unknown kinds") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"path","type":"shape","size":[64,64],"shape":{
        "type":"path","path":[{"type":"move_to","point":[0,0]},
                                  {"type":"line_to","point":[8,0]}],
        "operators":[{"kind":"repeater","params":{"copies":3,
          "position":[4,0],"opacity_decay":0.5}},
          {"kind":"round_corners","params":{"radius":2}},
          {"kind":"zig_zag","params":{"size":2,"points_per_unit":0.2,"smooth":true}},
          {"kind":"offset_path","params":{"amount":2,"miter_limit":3}},
          {"kind":"pucker_bloat","params":{"amount":0.5,"anchor":[1,2]}},
          {"kind":"twist","params":{"degrees":90,"center":[0,0]}},
          {"kind":"wiggle_path","params":{"amount":2,"frequency":0.75,"seed":1234}},
          {"kind":"merge","params":{"paths":[[{"type":"move_to","point":[10,10]},
            {"type":"quadratic_to","point":[12,12],"control1":[12,10]},
            {"type":"close"}]]}}]},
      "mask":{"type":"path","path":[{"type":"move_to","point":[-4,-4]},
        {"type":"line_to","point":[4,-4]},{"type":"line_to","point":[4,4]},
        {"type":"line_to","point":[-4,4]},{"type":"close"}],
        "feather":2,"expansion":1}}],
      "output":{"path":"out.png"}
    })JSON");
    auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = decoded.has_value() ? std::string{} : decoded.error().message;
    INFO(decode_error);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->layers[0].shape->operators.size() == 8);
    CHECK(decoded->layers[0].shape->operators[0].repeater.copies == 3);
    CHECK(decoded->layers[0].shape->operators[0].repeater.position[0] == doctest::Approx(4));
    CHECK(decoded->layers[0].shape->operators[1].kind ==
          chronon3d::render_plan::PathOperatorKindPlan::RoundCorners);
    CHECK(decoded->layers[0].shape->operators[1].radius == doctest::Approx(2));
    CHECK(decoded->layers[0].shape->operators[2].kind ==
          chronon3d::render_plan::PathOperatorKindPlan::ZigZag);
    CHECK(decoded->layers[0].shape->operators[2].smooth);
    CHECK(decoded->layers[0].shape->operators[3].kind ==
          chronon3d::render_plan::PathOperatorKindPlan::OffsetPath);
    CHECK(decoded->layers[0].shape->operators[3].amount == doctest::Approx(2));
    CHECK(decoded->layers[0].shape->operators[4].kind ==
          chronon3d::render_plan::PathOperatorKindPlan::PuckerBloat);
    CHECK(decoded->layers[0].shape->operators[4].anchor[1] == doctest::Approx(2));
    CHECK(decoded->layers[0].shape->operators[5].kind ==
          chronon3d::render_plan::PathOperatorKindPlan::Twist);
    CHECK(decoded->layers[0].shape->operators[5].degrees == doctest::Approx(90));
    CHECK(decoded->layers[0].shape->operators[6].kind ==
          chronon3d::render_plan::PathOperatorKindPlan::WigglePath);
    CHECK(decoded->layers[0].shape->operators[6].seed == 1234);
    CHECK(decoded->layers[0].shape->operators[7].kind ==
          chronon3d::render_plan::PathOperatorKindPlan::Merge);
    REQUIRE(decoded->layers[0].shape->operators[7].paths.size() == 1);
    CHECK(decoded->layers[0].shape->operators[7].paths[0].size() == 3);
    REQUIRE(decoded->layers[0].path_mask.has_value());
    CHECK(decoded->layers[0].path_mask->feather == doctest::Approx(2));
    CHECK(decoded->layers[0].path_mask->path.size() == 5);
    source["layers"][0]["shape"]["operators"][7]["kind"] = "future_operator";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan decodes LUT assets and fails closed on unsupported effects") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "graded"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
         {"effects", {{{"type", "lut_3d"}, {"asset", "looks/cinema.cube"}, {"amount", 0.75}}}}}
    }));
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->layers[0].effects.size() == 1);
    CHECK(decoded->layers[0].effects[0].asset == "looks/cinema.cube");
    CHECK(decoded->layers[0].effects[0].amount == doctest::Approx(0.75f));
    plan["layers"][0]["effects"][0]["type"] = "unknown";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan).has_value());
}

TEST_CASE("render plan decodes bounded Echo parameters") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "trail"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
         {"effects", {{{"type", "echo"}, {"echoes", 4}, {"decay", 0.6}, {"time_offset_frames", 2}}}}}
    }));
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 1);
    CHECK(decoded->layers[0].effects[0].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::Echo);
    CHECK(decoded->layers[0].effects[0].echoes == 4);
    plan["layers"][0]["effects"][0]["echoes"] = 9;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
}

TEST_CASE("render plan decodes bounded luma and chroma key parameters") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "keyed"}, {"type", "color"}, {"color", {0.2, 0.7, 0.1, 1.0}},
         {"effects", {{{"type", "keying"}, {"mode", "chroma"},
                       {"key_color", {0.0, 1.0, 0.0}}, {"threshold", 0.22},
                       {"softness", 0.1}, {"spill", 0.8}}}}}
    }));
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    const auto& effect = decoded->layers[0].effects[0];
    CHECK(effect.kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::Keying);
    CHECK(effect.keying_mode == chronon3d::render_plan::LayerPlan::KeyingModePlan::Chroma);
    CHECK(effect.threshold == doctest::Approx(0.22f));
    plan["layers"][0]["effects"][0]["mode"] = "future";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
    plan["layers"][0]["effects"][0]["mode"] = "luma";
    plan["layers"][0]["effects"][0]["spill"] = 2.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
}

TEST_CASE("render plan decodes bounded Threshold and Mosaic parameters") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "styled"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
         {"effects", {{{"type", "threshold"}, {"threshold", 0.4}, {"softness", 0.15}},
                       {{"type", "mosaic"}, {"block_size", 12}}}}}
    }));
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 2);
    CHECK(decoded->layers[0].effects[0].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::Threshold);
    CHECK(decoded->layers[0].effects[0].threshold_softness == doctest::Approx(0.15f));
    CHECK(decoded->layers[0].effects[1].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::Mosaic);
    CHECK(decoded->layers[0].effects[1].block_size == 12);
    plan["layers"][0]["effects"][1]["block_size"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
}

TEST_CASE("render plan decoder rejects a layer with missing id or type") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 640}, {"height", 360}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 30}}},
        {"layers", {{{{"text", "no id and no type"}}}}},
        {"output", {{"path", "out.mp4"}}}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE_FALSE(decoded.has_value());
    CHECK_FALSE(decoded.error().message.empty());
}

TEST_CASE("render plan decoder fails closed when 3D tracks omit enable_3d") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 640}, {"height", 360}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 30}}},
        {"layers", {{{"id", "title"}, {"type", "text"}, {"text", "Hello"},
                      {"position", {320.0, 180.0}},
                      {"style", {{"font", "fonts/roboto.ttf"}, {"font_size", 24.0}, {"fill", "#FFFFFF"}}},
                      {"animation", {{"tracks", {{{"property", "rotation_y"},
                                                     {"keyframes", {{{"frame", 0}, {"value", -18.0}},
                                                                     {{"frame", 10}, {"value", 0.0}}}}}}}}}}}},
        {"output", {{"path", "out.mp4"}}}};

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error().path == "layers[0].enable_3d");
    CHECK(decoded.error().code == "3d_requires_explicit_enable");
}

TEST_CASE("render plan layer rotation contract is expressed in degrees") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 640}, {"height", 360}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 30}}},
        {"layers", {{{"id", "title"}, {"type", "text"}, {"text", "Hello"},
                      {"position", {320.0, 180.0}}, {"rotation", {0.0, 22.0, 0.0}},
                      {"enable_3d", true},
                      {"style", {{"font", "fonts/roboto.ttf"}, {"font_size", 24.0},
                                  {"fill", "#FFFFFF"}}}}}},
        {"output", {{"path", "out.mp4"}}}};

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    CHECK(decoded->layers[0].rotation[1] == doctest::Approx(22.0f));
}

TEST_CASE("render plan decoder returns validation path on malformed plan") {
    const nlohmann::json source = {{"schema", "chronon.render-plan.v2"}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE_FALSE(decoded.has_value());
    CHECK_FALSE(decoded.error().message.empty());
}

TEST_CASE("render plan decoder rejects absolute and traversal asset references") {
    const nlohmann::json base = {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 1}}},
        {"layers", {{{"id", "image"}, {"type", "image"},
                      {"asset", "image.png"}}}},
        {"output", {{"path", "out.png"}}}};

    auto absolute = base;
    absolute["layers"][0]["asset"] = "/tmp/image.png";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(absolute).has_value());

    auto traversal = base;
    traversal["layers"][0]["asset"] = "../image.png";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(traversal).has_value());
}

TEST_CASE("render plan budget rejects excessive layer count") {
    chronon3d::render_plan::RenderPlan plan;
    plan.canvas.width = 320;
    plan.canvas.height = 180;
    plan.canvas.fps = chronon3d::FrameRate{30, 1};
    plan.canvas.duration = chronon3d::Frame{10};
    plan.layers.resize(2);

    chronon3d::render_plan::RenderBudget budget;
    budget.max_layers = 1;
    const auto error = chronon3d::render_plan::validate_render_plan_budget(plan, budget);
    REQUIRE(error.has_value());
    CHECK(error->path == "layers");
}

namespace {

chronon3d::render_plan::RenderPlan budget_plan() {
    chronon3d::render_plan::RenderPlan plan;
    plan.canvas = {.width = 320, .height = 180, .fps = chronon3d::FrameRate{30, 1},
                   .duration = chronon3d::Frame{30}};
    return plan;
}

} // namespace

TEST_CASE("validate_render_budget rejects resolution, fps, and duration limits") {
    auto plan = budget_plan();
    chronon3d::render_plan::RenderBudget budget;

    plan.canvas.width = 0;
    auto error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "canvas.width");

    plan = budget_plan();
    plan.canvas.height = static_cast<int>(budget.max_height) + 1;
    error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "canvas.height");

    plan = budget_plan();
    plan.canvas.fps = chronon3d::FrameRate{0, 1};
    error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "canvas.fps");

    plan = budget_plan();
    plan.canvas.duration = chronon3d::Frame{-1};
    error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "canvas.duration_frames");
}

TEST_CASE("validate_render_budget rejects pixel, frame, layer, and memory limits") {
    auto plan = budget_plan();
    chronon3d::render_plan::RenderBudget budget;

    budget.max_total_pixels = 100;
    auto error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "canvas");

    plan = budget_plan();
    budget = {};
    budget.max_frames = 29;
    error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "canvas.duration_frames");

    plan = budget_plan();
    chronon3d::render_plan::LayerPlan layer;
    plan.layers.push_back(layer);
    budget = {};
    budget.max_layers = 0;
    error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "layers");

    plan = budget_plan();
    budget = {};
    budget.max_peak_memory_bytes = 320 * 180 * 16 - 1;
    error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "canvas");
}

TEST_CASE("validate_render_budget rejects text, asset references, and audio limits") {
    auto plan = budget_plan();
    chronon3d::render_plan::LayerPlan text;
    text.type = chronon3d::render_plan::LayerType::Text;
    text.text = "too long";
    plan.layers.push_back(text);
    chronon3d::render_plan::RenderBudget budget;
    budget.max_text_bytes = 3;
    auto error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "layers[].text");

    plan = budget_plan();
    chronon3d::render_plan::LayerPlan asset;
    asset.asset = "images/asset.png";
    plan.layers.push_back(asset);
    budget = {};
    budget.max_asset_reference_bytes = 4;
    error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "layers[]");

    plan = budget_plan();
}

TEST_CASE("validate_render_budget rejects layer timing, output estimate, and non-finite values") {
    auto plan = budget_plan();
    chronon3d::render_plan::LayerPlan layer;
    layer.start_frame = chronon3d::Frame{30};
    plan.layers.push_back(layer);
    chronon3d::render_plan::RenderBudget budget;
    auto error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "layers[0].start_frame");

    plan = budget_plan();
    budget.max_estimated_output_bytes = 320 * 180 * 4 * 30 - 1;
    error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "canvas");

    plan = budget_plan();
    plan.output.bitrate = -1;
    error = chronon3d::render_plan::validate_render_budget(plan, budget);
    REQUIRE(error);
    CHECK(error->path == "output.bitrate");
}

TEST_CASE("RenderBudget carries the canonical temporal pixel policy") {
    chronon3d::render_plan::RenderBudget budget;
    CHECK(budget.max_temporal_pixels == 128ULL * 1024ULL * 1024ULL);
    budget.max_temporal_pixels = 4096;
    CHECK(budget.max_temporal_pixels == 4096);
}

TEST_CASE("render plan decoder uses fail-loud budget phase") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 1}}},
        {"layers", nlohmann::json::array()},
        {"output", {{"path", "out.png"}}}};
    auto over_budget = source;
    over_budget["canvas"]["width"] = 7681;
    const auto decoded = chronon3d::render_plan::decode_render_plan(over_budget);
    CHECK_FALSE(decoded);

    const auto valid = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(valid);
    auto budgeted = source;
    budgeted["budget"]["max_temporal_pixels"] = 4096;
    const auto decoded_budgeted = chronon3d::render_plan::decode_render_plan(budgeted);
    REQUIRE(decoded_budgeted);
    CHECK(decoded_budgeted->budget.max_temporal_pixels == 4096);
    chronon3d::render_plan::RenderBudget tight;
    tight.max_width = 100;
    CHECK(chronon3d::render_plan::validate_render_budget(valid.value(), tight));
}

TEST_CASE("render plan fingerprint includes decoded content and preserves order") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 640}, {"height", 360}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 12}}},
        {"layers", {{{"id", "first"}, {"type", "color"},
                      {"color", {1.0, 0.0, 0.0, 1.0}}},
                     {{"id", "second"}, {"type", "color"},
                      {"color", {0.0, 1.0, 0.0, 1.0}}}}},
        {"output", {{"path", "out.png"}}}};

    const auto original = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(original.has_value());
    CHECK(original->content_fingerprint ==
          chronon3d::render_plan::compute_render_plan_content_fingerprint(*original));

    const auto repeat = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeat.has_value());
    CHECK(original->content_fingerprint == repeat->content_fingerprint);

    auto changed_color = source;
    changed_color["layers"][0]["color"] = {0.5, 0.0, 0.0, 1.0};
    const auto changed = chronon3d::render_plan::decode_render_plan(changed_color);
    REQUIRE(changed.has_value());
    CHECK(original->content_fingerprint != changed->content_fingerprint);

    auto reordered = source;
    std::swap(reordered["layers"][0], reordered["layers"][1]);
    const auto reordered_plan = chronon3d::render_plan::decode_render_plan(reordered);
    REQUIRE(reordered_plan.has_value());
    CHECK(original->content_fingerprint != reordered_plan->content_fingerprint);

    auto changed_budget = source;
    changed_budget["budget"]["max_temporal_pixels"] = 4096;
    const auto budget_plan = chronon3d::render_plan::decode_render_plan(changed_budget);
    REQUIRE(budget_plan.has_value());
    CHECK(original->content_fingerprint != budget_plan->content_fingerprint);
}

TEST_CASE("render plan decoder decodes concrete animation tracks") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {{"width", 1920}, {"height", 1080}, {"fps_num", 30}, {"fps_den", 1},
                     {"duration_frames", 90}}},
        {"layers", {{
            {"id", "person_01"},
            {"type", "text"},
            {"text", "Tim Cook"},
            {"style", {
                {"font", "fonts/Poppins-Bold.ttf"},
                {"font_size", 58},
                {"fill", "#FFFFFF"},
                {"stroke", {{"color", "#000000"}, {"width", 2}}},
                {"shadow", {{"color", "#000000"}, {"opacity", 0.65},
                             {"blur", 16}, {"offset", {0, 6}}}},
                {"glow", {{"radius", 42}, {"intensity", 0.25},
                           {"color", "#FFFFFF"}}},
                {"background", {{"color", "#050509"}, {"opacity", 0.86},
                                 {"radius", 12}, {"padding", {24, 14}}}}
            }},
            {"animation", {
                {"tracks", nlohmann::json::array({
                    {
                        {"property", "opacity"},
                        {"easing", "in_quad"},
                        {"keyframes", nlohmann::json::array({
                            {{"frame", 0}, {"value", 0.0}},
                            {{"frame", 15}, {"value", 1.0}}
                        })}
                    }
                })}
            }}
        }}},
        {"output", {{"path", "out.mp4"}}}};

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded.has_value());
    const auto& layer = decoded->layers[0];

    REQUIRE(layer.style.has_value());
    CHECK(layer.font == "fonts/Poppins-Bold.ttf");
    CHECK(layer.style->font_size.value() == doctest::Approx(58.0f));
    CHECK(layer.style->fill == "#FFFFFF");
    REQUIRE(layer.style->stroke.has_value());
    CHECK(layer.style->stroke->color == "#000000");
    REQUIRE(layer.style->shadow.has_value());
    CHECK(layer.style->shadow->blur.value() == doctest::Approx(16.0f));
    CHECK(layer.style->shadow->offset[1] == doctest::Approx(6.0f));
    REQUIRE(layer.style->glow.has_value());
    CHECK(layer.style->glow->radius == doctest::Approx(42.0f));
    CHECK(layer.style->glow->intensity == doctest::Approx(0.25f));
    CHECK(layer.style->glow->color == "#FFFFFF");
    REQUIRE(layer.style->background.has_value());
    CHECK(layer.style->background->radius.value() == doctest::Approx(12.0f));
    CHECK(layer.style->background->padding[0] == doctest::Approx(24.0f));

    REQUIRE(layer.animation.has_value());
    REQUIRE(layer.animation->tracks.size() == 1);
    CHECK(layer.animation->tracks[0].property == "opacity");
    CHECK(layer.animation->tracks[0].keyframes.size() == 2);
}

TEST_CASE("render plan decoder ignores legacy glow controls in content and fingerprint") {
    const nlohmann::json base = {
        {"schema", "chronon.render-plan.v2"}, {"version", 2},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 1}}},
        {"layers", {{{"id", "title"}, {"type", "text"}, {"text", "Glow"},
                      {"style", {{"font", "fonts/roboto.ttf"}, {"font_size", 24.0},
                                 {"fill", "#FFFFFF"},
                                 {"glow", {{"radius", 18.0}, {"intensity", 0.4},
                                            {"color", "#80C0FF"}}}}}}}},
        {"output", {{"path", "out.png"}}}};
    auto with_legacy_controls = base;
    with_legacy_controls["layers"][0]["style"]["glow"].update({
        {"threshold", 0.7}, {"falloff", 1.8}, {"core_strength", 0.6},
        {"aura_strength", 0.3}, {"bloom_strength", 0.2}, {"high_quality", true}});

    const auto current = chronon3d::render_plan::decode_render_plan(base);
    const auto legacy = chronon3d::render_plan::decode_render_plan(with_legacy_controls);
    if (!current) INFO(current.error().message);
    if (!legacy) INFO(legacy.error().message);
    REQUIRE(current);
    REQUIRE(legacy);
    REQUIRE(current->layers[0].style);
    REQUIRE(current->layers[0].style->glow);
    REQUIRE(legacy->layers[0].style);
    REQUIRE(legacy->layers[0].style->glow);
    CHECK(legacy->layers[0].style->glow->radius == doctest::Approx(18.0f));
    CHECK(legacy->layers[0].style->glow->intensity == doctest::Approx(0.4f));
    CHECK(legacy->layers[0].style->glow->color == "#80C0FF");
    CHECK(legacy->content_fingerprint == current->content_fingerprint);
}

TEST_CASE("render plan decoder accepts camera pose and closed camera tracks") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"}, {"version", 2},
        {"canvas", {{"width", 640}, {"height", 360}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 48}}},
        {"camera", {{"type", "perspective"}, {"position", {0, 0, -1000}},
                     {"rotation_deg", {0, 0, 0}}, {"fov_deg", 50},
                     {"near", 1}, {"far", 10000}, {"zoom", 1}}},
        {"camera_animation", {{"tracks", {{{"property", "camera_position_z"},
            {"easing", "out_cubic"}, {"keyframes", {{{"frame", 0}, {"value", -600}},
                                                         {{"frame", 48}, {"value", -1000}}}}}}}}},
        {"layers", {{{"id", "bg"}, {"type", "color"}, {"color", {0, 0, 0, 1}}}}},
        {"output", {{"path", "out.png"}}}};

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->camera);
    CHECK(decoded->camera->position[2] == doctest::Approx(-1000.0f));
    REQUIRE(decoded->camera_animation);
    CHECK(decoded->camera_animation->at(0).property ==
          chronon3d::render_plan::CameraPropertyPlan::PositionZ);
}

TEST_CASE("render plan decoder accepts bounded cubic Bezier track easing and fingerprints it") {
    auto source = nlohmann::json{
        {"schema", "chronon.render-plan.v2"}, {"version", 2},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 30}}},
        {"layers", {{{"id", "move"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
            {"animation", {{"tracks", {{{"property", "position_y"},
                {"easing", "bezier"}, {"bezier_curve", {0.16, 1.0, 0.3, 1.0}},
                {"keyframes", {{{"frame", 0}, {"value", 100}},
                                {{"frame", 24}, {"value", 0}}}}}}}}}}}},
        {"output", {{"path", "out.png"}}}};
    const auto first = chronon3d::render_plan::decode_render_plan(source);
    if (!first) FAIL(first.error().message);
    REQUIRE(first);
    const auto& track = first->layers[0].animation->tracks[0];
    REQUIRE(track.bezier_curve);
    CHECK((*track.bezier_curve)[0] == doctest::Approx(0.16f));
    CHECK((*track.bezier_curve)[3] == doctest::Approx(1.0f));

    auto changed = source;
    changed["layers"][0]["animation"]["tracks"][0]["bezier_curve"][1] = 0.8;
    const auto second = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(second);
    CHECK(first->content_fingerprint != second->content_fingerprint);

    auto missing_curve = source;
    missing_curve["layers"][0]["animation"]["tracks"][0].erase("bezier_curve");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(missing_curve));
    auto invalid_x = source;
    invalid_x["layers"][0]["animation"]["tracks"][0]["bezier_curve"][0] = 1.1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_x));
}

TEST_CASE("render plan decoder rejects invalid camera ranges and unknown tracks") {
    const nlohmann::json base = {
        {"schema", "chronon.render-plan.v2"}, {"version", 2},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 1}}},
        {"camera", {{"type", "perspective"}, {"fov_deg", 50}, {"near", 1},
                     {"far", 10000}, {"zoom", 1}}},
        {"layers", {{{"id", "bg"}, {"type", "color"}, {"color", {0, 0, 0, 1}}}}},
        {"output", {{"path", "out.png"}}}};
    auto bad_fov = base;
    bad_fov["camera"]["fov_deg"] = 121;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_fov));

    auto bad_track = base;
    bad_track["camera_animation"] = {{"tracks", {{{"property", "camera_focus"},
        {"keyframes", {{{"frame", 0}, {"value", 1}}}}}}}};
    const auto error = chronon3d::render_plan::decode_render_plan(bad_track);
    REQUIRE_FALSE(error);
    CHECK(error.error().message.find("camera_focus") != std::string::npos);
}

TEST_CASE("render plan decoder enforces depth role conflict and fingerprints it") {
    const nlohmann::json base = {
        {"schema", "chronon.render-plan.v2"}, {"version", 2},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 1}}},
        {"layers", {{{"id", "subject"}, {"type", "color"},
                      {"color", {1, 1, 1, 1}}, {"enable_3d", true},
                      {"position", {0, 0, 10}}, {"depth_role", "subject"}}}},
        {"output", {{"path", "out.png"}}}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(base));

    auto far = base;
    far["layers"][0].erase("position");
    far["layers"][0]["depth_role"] = "far_background";
    auto near = far;
    near["layers"][0]["depth_role"] = "foreground";
    const auto far_plan = chronon3d::render_plan::decode_render_plan(far);
    const auto near_plan = chronon3d::render_plan::decode_render_plan(near);
    REQUIRE(far_plan);
    REQUIRE(near_plan);
    CHECK(far_plan->content_fingerprint != near_plan->content_fingerprint);
}

TEST_CASE("V3 render-plan decoder accepts all five concrete shape contents") {
    using namespace chronon3d;
    struct ShapeCase {
        const char* name;
        nlohmann::json shape;
        render_plan::ShapeContentTypePlan expected;
    };
    const std::vector<ShapeCase> cases{
        {"rect", {{"type", "rect"}, {"fill", {0.2, 0.3, 0.4, 1.0}}},
         render_plan::ShapeContentTypePlan::Rect},
        {"rounded_rect", {{"type", "rounded_rect"}, {"radius", 8.0}},
         render_plan::ShapeContentTypePlan::RoundedRect},
        {"ellipse", {{"type", "ellipse"}},
         render_plan::ShapeContentTypePlan::Ellipse},
        {"line", {{"type", "line"},
                   {"path", {{{"type", "move_to"}, {"point", {0, 0}}},
                             {{"type", "line_to"}, {"point", {20, 10}}}}},
                   {"stroke", {{"color", "#FFFFFF"}, {"width", 2.0}}}},
         render_plan::ShapeContentTypePlan::Line},
        {"path", {{"type", "path"},
                   {"path", {{{"type", "move_to"}, {"point", {0, 0}}},
                             {{"type", "quadratic_to"}, {"control1", {10, 20}}, {"point", {20, 0}}},
                             {{"type", "cubic_to"}, {"control1", {25, -5}}, {"control2", {35, 5}}, {"point", {40, 0}}},
                             {{"type", "close"}}}}},
         render_plan::ShapeContentTypePlan::Path}
    };
    for (const auto& test : cases) {
        INFO("shape content: " << test.name);
        auto layer = render_plan_test::shape_layer(test.shape);
        auto input = render_plan_test::base_plan({layer});
        const auto decoded = render_plan::decode_render_plan(input);
        if (!decoded) INFO(decoded.error().message);
        REQUIRE(decoded);
        REQUIRE(decoded->layers.size() == 1);
        REQUIRE(decoded->layers[0].shape);
        CHECK(decoded->layers[0].shape->type == test.expected);
        if (test.expected == render_plan::ShapeContentTypePlan::RoundedRect)
            CHECK(decoded->layers[0].shape->radius == doctest::Approx(8.0f));
        if (test.expected == render_plan::ShapeContentTypePlan::Rect)
            CHECK(decoded->layers[0].shape->fill_color->at(0) == doctest::Approx(0.2f));
        if (test.expected == render_plan::ShapeContentTypePlan::Line) {
            REQUIRE(decoded->layers[0].shape->stroke);
            CHECK(decoded->layers[0].shape->stroke->width == doctest::Approx(2.0f));
        }
    }

    auto unsupported = render_plan_test::shape_layer({{"type", "hexagon"}});
    const auto unknown = render_plan::decode_render_plan(
        render_plan_test::base_plan({unsupported}));
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().message.find("layers[0].shape.type") != std::string::npos);
    CHECK(unknown.error().message.find("required enum") != std::string::npos);
}

TEST_CASE("V3 shape path commands decode in order and malformed commands fail closed") {
    using namespace chronon3d;
    auto layer = render_plan_test::shape_layer({
        {"type", "path"},
        {"path", nlohmann::json::array({
            {{"type", "move_to"}, {"point", {0, 0}}},
            {{"type", "line_to"}, {"point", {1, 1}}},
            {{"type", "quadratic_to"}, {"control1", {2, 3}}, {"point", {4, 4}}},
            {{"type", "cubic_to"}, {"control1", {5, 6}}, {"control2", {7, 8}}, {"point", {9, 9}}},
            {{"type", "close"}}
        })}
    });
    auto input = render_plan_test::base_plan({layer});
    auto decoded = chronon3d::render_plan::decode_render_plan(input);
    if (!decoded) INFO(decoded.error().message);
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded);
    const auto& commands = decoded->layers[0].shape->path;
    REQUIRE(commands.size() == 5);
    CHECK(commands[0].type == render_plan::ShapePathCommandTypePlan::MoveTo);
    CHECK(commands[1].type == render_plan::ShapePathCommandTypePlan::LineTo);
    CHECK(commands[2].type == render_plan::ShapePathCommandTypePlan::QuadraticTo);
    CHECK(commands[3].type == render_plan::ShapePathCommandTypePlan::CubicTo);
    CHECK(commands[4].type == render_plan::ShapePathCommandTypePlan::Close);

    auto unknown = input;
    unknown["layers"][0]["shape"]["path"][1]["type"] = "arc_to";
    const auto unknown_error = render_plan::decode_render_plan(unknown);
    REQUIRE_FALSE(unknown_error);
    CHECK(unknown_error.error().message.find("layers[0].shape.path[1].type") != std::string::npos);
    CHECK(unknown_error.error().message.find("required enum") != std::string::npos);

    auto wrong_arity = input;
    wrong_arity["layers"][0]["shape"]["path"][1]["type"] = "line_to";
    wrong_arity["layers"][0]["shape"]["path"][1]["point"] = {1};
    const auto arity_error = render_plan::decode_render_plan(wrong_arity);
    REQUIRE_FALSE(arity_error);
    CHECK(arity_error.error().message.find("layers[0].shape.path[1].point") != std::string::npos);
    CHECK(arity_error.error().message.find("array has too few items") != std::string::npos);
}

TEST_CASE("V3 shape decoding rejects non-finite geometry and oversized rounded corners") {
    using namespace chronon3d;
    auto point_shape = nlohmann::json{
        {"type", "path"},
        {"path", nlohmann::json::array({
            {{"type", "move_to"}, {"point", {0.0, 0.0}}},
            {{"type", "line_to"}, {"point", {std::numeric_limits<double>::infinity(), 1.0}}}
        })}
    };
    auto point_layer = render_plan_test::shape_layer(std::move(point_shape));
    auto input = render_plan_test::base_plan({point_layer});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(input));

    auto rounded = render_plan_test::shape_layer({{"type", "rounded_rect"}, {"radius", 15.0}});
    input = render_plan_test::base_plan({rounded});
    REQUIRE(render_plan::decode_render_plan(input));
    input["layers"][0]["shape"]["radius"] = 15.01;
    const auto radius_error = render_plan::decode_render_plan(input);
    REQUIRE_FALSE(radius_error);
    CHECK(radius_error.error().message.find("rounded_rect radius") != std::string::npos);

    auto bad_size = render_plan_test::shape_layer({{"type", "rect"}});
    bad_size["size"][0] = std::numeric_limits<double>::infinity();
    CHECK_FALSE(render_plan::decode_render_plan(render_plan_test::base_plan({bad_size})));
}

TEST_CASE("V3 rounded_rect accepts four corner radii and rejects ambiguous or invalid values") {
    auto layer = render_plan_test::shape_layer({
        {"type", "rounded_rect"}, {"corner_radius", {14.0, 14.0, 0.0, 14.0}}});
    auto input = render_plan_test::base_plan({layer});
    auto decoded = chronon3d::render_plan::decode_render_plan(input);
    INFO("render plan decode failure: " << decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].shape->corner_radius);
    CHECK((*decoded->layers[0].shape->corner_radius)[2] == doctest::Approx(0.0f));

    input["layers"][0]["shape"]["radius"] = 8.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(input));
    input["layers"][0]["shape"].erase("radius");
    input["layers"][0]["shape"]["corner_radius"][0] = 15.01;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(input));
    input["layers"][0]["shape"]["corner_radius"] = {1.0, 2.0, 3.0};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(input));
}

TEST_CASE("Render plan shadow arrays are bounded and mutually exclusive with legacy shadow") {
    auto layer = render_plan_test::shape_layer({{"type", "rounded_rect"}});
    layer["style"] = {{"shadows", nlohmann::json::array({
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}, {"blur", 8.0}},
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}, {"blur", 24.0}}})}};
    auto plan = render_plan_test::base_plan({layer});
    auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].style);
    CHECK(decoded->layers[0].style->shadows.size() == 2);
    plan["layers"][0]["style"]["shadow"] = {{"color", "#000000"}, {"offset", {0, 0}}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
    plan["layers"][0]["style"].erase("shadow");
    plan["layers"][0]["style"]["shadows"].push_back(
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}});
    plan["layers"][0]["style"]["shadows"].push_back(
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
}

TEST_CASE("Render plan variables resolve typed values before validation and fingerprinting") {
    nlohmann::json layer{
        {"id", "title"}, {"type", "text"}, {"text", "Hello ${title}"},
        {"style", {{"font", "font.ttf"}, {"font_size", 20}, {"fill", "#FFFFFF"}}},
        {"animation", {{"tracks", nlohmann::json::array({
            {{"property", "opacity"}, {"keyframes", nlohmann::json::array({
                {{"frame", 0}, {"value", "${score}"}}
            })}}
        })}}}
    };
    auto source = render_plan_test::base_plan({layer});
    source["schema"] = "chronon.render-plan.v2";
    source["version"] = 2;
    source["variables"] = {{"title", "Ada"}, {"score", 73}, {"unused", "x"}};
    auto resolved = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(resolved);
    CHECK(resolved->layers[0].text == "Hello Ada");
    REQUIRE(resolved->layers[0].animation);
    CHECK(resolved->layers[0].animation->tracks[0].keyframes[0].value[0] == doctest::Approx(73.0f));
    REQUIRE(resolved->warnings.size() == 1);
    auto manual = source;
    manual.erase("variables");
    manual["layers"][0]["text"] = "Hello Ada";
    manual["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["value"] = 73;
    auto hand_written = chronon3d::render_plan::decode_render_plan(manual);
    REQUIRE(hand_written);
    CHECK(resolved->content_fingerprint == hand_written->content_fingerprint);
    source["layers"][0]["text"] = "${missing}";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
    source["layers"][0]["text"] = "ok";
    source["layers"][0]["animation"]["tracks"][0]["property"] = "${score}";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
}

TEST_CASE("Render plan declares PNG sequence and GIF output formats") {
    auto source = render_plan_test::base_plan();
    source["output"]["format"] = "png_sequence";
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    CHECK(decoded->output.format == chronon3d::render_plan::OutputFormat::PngSequence);
    source["output"]["format"] = "gif";
    const auto gif = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(gif);
    CHECK(gif->output.format == chronon3d::render_plan::OutputFormat::Gif);
}

TEST_CASE("Backdrop blur is accepted only on shape layers with bounded parameters") {
    auto shape = render_plan_test::shape_layer({{"type", "rounded_rect"}});
    shape["style"] = {{"backdrop", {{"blur", 16.0}, {"brightness", 1.0}, {"saturation", 1.0}}}};
    auto plan = render_plan_test::base_plan({shape});
    auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].style->backdrop);
    CHECK(decoded->layers[0].style->backdrop->blur == doctest::Approx(16.0f));
    plan["layers"][0]["style"]["backdrop"]["blur"] = 65.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
    plan = render_plan_test::base_plan({{{"id", "txt"}, {"type", "text"}, {"text", "x"},
        {"style", {{"font", "font.ttf"}, {"font_size", 10}, {"fill", "#FFFFFF"},
                    {"backdrop", {{"blur", 16}}}}}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
}

TEST_CASE("V3 color alias normalizes to shape.fill with explicit fill precedence") {
    using namespace chronon3d;
    auto layer = render_plan_test::shape_layer({{"type", "rect"}});
    layer["color"] = {0.2, 0.3, 0.4, 1.0};
    auto input = render_plan_test::base_plan({layer});
    auto alias = render_plan::decode_render_plan(input);
    REQUIRE(alias);
    REQUIRE(alias->layers[0].shape->fill_color);
    CHECK(alias->layers[0].shape->fill_color->at(0) == doctest::Approx(0.2f));
    CHECK(alias->layers[0].color[0] == doctest::Approx(0.2f));

    input["layers"][0]["shape"]["fill"] = {0.8, 0.7, 0.6, 1.0};
    const auto both = render_plan::decode_render_plan(input);
    REQUIRE(both);
    CHECK(both->layers[0].shape->fill_color->at(0) == doctest::Approx(0.8f));
    CHECK(both->layers[0].color[0] == doctest::Approx(0.8f));

    const auto legacy = render_plan::decode_render_plan(render_plan_test::v2_legacy_plan());
    REQUIRE(legacy);
    CHECK(legacy->layers[0].type == render_plan::LayerType::Color);
    CHECK(legacy->layers[0].color[0] == doctest::Approx(0.1f));
    CHECK_FALSE(legacy->layers[0].shape);
}

TEST_CASE("V3 gradient decoding requires two stops and preserves spread and opacity stops") {
    using namespace chronon3d;
    const auto gradient = render_plan_test::gradient();
    auto layer = render_plan_test::shape_layer({{"type", "rect"}, {"fill", gradient}});
    auto input = render_plan_test::base_plan({layer});
    const auto decoded = render_plan::decode_render_plan(input);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].shape->gradient);
    CHECK(decoded->layers[0].shape->gradient->spread == graphics::GradientSpread::Reflect);
    CHECK(decoded->layers[0].shape->gradient->opacity_stops.size() == 2);
    auto fewer_stops = input;
    fewer_stops["layers"][0]["shape"]["fill"]["color_stops"].erase(1);
    CHECK_FALSE(render_plan::decode_render_plan(fewer_stops));

    auto solid_or_gradient = render_plan_test::shape_layer({
        {"type", "rect"}, {"fill", gradient}});
    const auto gradient_plan = render_plan::decode_render_plan(
        render_plan_test::base_plan({solid_or_gradient}));
    REQUIRE(gradient_plan);
    REQUIRE(gradient_plan->layers[0].shape->gradient);
    CHECK_FALSE(gradient_plan->layers[0].shape->fill_color);
}
