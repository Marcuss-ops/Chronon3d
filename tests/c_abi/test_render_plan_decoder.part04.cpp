#include "test_render_plan_decoder_test_support.hpp"

TEST_CASE("render plan V3 decodes animated path morphs and fingerprints their full semantics") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"shape","type":"shape","size":[32,32],"shape":{
        "type":"path","path":[
          {"type":"move_to","point":[0,0]},
          {"type":"cubic_to","point":[10,10],"control1":[3,0],"control2":[7,10]},
          {"type":"close"}],
        "operators":[{"kind":"morph","params":{
          "target":[{"type":"move_to","point":[2,4]},
            {"type":"cubic_to","point":[20,20],"control1":[6,4],"control2":[14,20]},
            {"type":"close"}],
          "progress":0.25,
          "animation":{"easing":"smoothstep","keyframes":[
            {"frame":0,"value":0},{"frame":12,"value":1}]}}}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = decoded.has_value() ? std::string{} :
        decoded.error().path + ": " + decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_error);
    const auto& morph = decoded->layers[0].shape->operators[0];
    CHECK(morph.kind == chronon3d::render_plan::PathOperatorKindPlan::Morph);
    CHECK(morph.morph_progress == doctest::Approx(0.25f));
    REQUIRE(morph.morph_target.size() == 3);
    REQUIRE(morph.morph_animation.has_value());
    CHECK(morph.morph_animation->keyframes.size() == 2);
    const auto repeated = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeated.has_value());
    CHECK(decoded->content_fingerprint == repeated->content_fingerprint);

    auto changed = source;
    changed["layers"][0]["shape"]["operators"][0]["params"]["target"][1]["point"][0] = 21;
    const auto changed_target = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_target.has_value());
    CHECK(decoded->content_fingerprint != changed_target->content_fingerprint);
    changed = source;
    changed["layers"][0]["shape"]["operators"][0]["params"]["animation"]["keyframes"][1]["value"] = 0.5;
    const auto changed_track = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_track.has_value());
    CHECK(decoded->content_fingerprint != changed_track->content_fingerprint);

    auto incompatible = source;
    incompatible["layers"][0]["shape"]["operators"][0]["params"]["target"][1]["type"] = "line_to";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(incompatible).has_value());
    auto invalid_progress = source;
    invalid_progress["layers"][0]["shape"]["operators"][0]["params"]["animation"]["keyframes"][0]["value"] = 2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_progress).has_value());
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

TEST_CASE("render plan decodes bounded Chromatic Aberration and Halftone") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "styled"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
         {"effects", {{{"type", "rgb_channel_transform"}, {"red_offset", 3.5}, {"blue_offset", -4.0},
                        {"red_transform", {{"translation", {4.0, -2.0}}, {"scale", {1.2, 0.8}},
                                            {"rotation", -5.0}, {"opacity", 0.75}}}},
                       {{"type", "halftone"}, {"size", 12}, {"angle", 22.5}, {"amount", 0.8}}}}}
    }));
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 2);
    CHECK(decoded->layers[0].effects[0].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::ChromaticAberration);
    CHECK(decoded->layers[0].effects[0].chromatic_red_offset == doctest::Approx(3.5f));
    CHECK(decoded->layers[0].effects[0].chromatic_blue_offset == doctest::Approx(-4.0f));
    CHECK(decoded->layers[0].effects[0].chromatic_red_transform[0] == doctest::Approx(4.0f));
    CHECK(decoded->layers[0].effects[0].chromatic_red_transform[2] == doctest::Approx(1.2f));
    CHECK(decoded->layers[0].effects[0].chromatic_red_transform[4] == doctest::Approx(-5.0f));
    CHECK(decoded->layers[0].effects[0].chromatic_red_transform[5] == doctest::Approx(0.75f));
    CHECK(decoded->layers[0].effects[1].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::Halftone);
    CHECK(decoded->layers[0].effects[1].halftone_size == 12);
    plan["layers"][0]["effects"][0]["red_offset"] = 129.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
    plan["layers"][0]["effects"][0]["red_offset"] = 2.0;
    plan["layers"][0]["effects"][1]["amount"] = 1.1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
    plan["layers"][0]["effects"][1]["amount"] = 0.8;
    plan["layers"][0]["effects"][0]["green_transform"] = {{"scale", {0.0, 1.0}}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
}

TEST_CASE("render plan decodes bounded Directional and Radial Blur effects") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "blurred"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
         {"effects", {{{"type", "directional_blur"}, {"angle", 30.0}, {"length", 24.0}, {"samples", 8}},
                       {{"type", "radial_blur"}, {"center", {0.25, 0.75}}, {"amount", 0.8},
                        {"render_samples", 12}, {"preview_samples", 6}}}}}
    }));
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 2);
    CHECK(decoded->layers[0].effects[0].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::DirectionalBlur);
    CHECK(decoded->layers[0].effects[0].directional_length == doctest::Approx(24.0f));
    CHECK(decoded->layers[0].effects[1].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::RadialBlur);
    CHECK(decoded->layers[0].effects[1].radial_center[1] == doctest::Approx(0.75f));
    plan["layers"][0]["effects"][1]["center"][0] = 1.1;
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

TEST_CASE("render plan decoder accepts ordered resolved camera samples") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan.v2"}, {"version", 2},
        {"canvas", {{"width", 640}, {"height", 360}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 48}}},
        {"camera", {{"type", "perspective"}, {"position", {0, 0, -1000}},
                     {"rotation_deg", {0, 0, 0}}, {"fov_deg", 50},
                     {"near", 1}, {"far", 10000}, {"zoom", 1}}},
        {"camera_samples", nlohmann::json::array({
            {{"frame", -0.25}, {"camera", {{"position", {0, 0, -600}}}},},
            {{"frame", 0}, {"camera", {{"position", {0, 0, -1000}}}}}})},
        {"layers", {{{"id", "bg"}, {"type", "color"}, {"color", {0, 0, 0, 1}}}}},
        {"output", {{"path", "out.png"}}}};

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->camera_samples);
    CHECK(decoded->camera_samples->size() == 2);
    auto unordered = source;
    std::swap(unordered["camera_samples"][0], unordered["camera_samples"][1]);
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(unordered));
    auto legacy = source;
    legacy["camera_animation"] = {{"tracks", nlohmann::json::array()}};
    const auto rejected = chronon3d::render_plan::decode_render_plan(legacy);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == "camera_animation_retired");
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

    auto bad_samples = base;
    bad_samples["camera_samples"] = nlohmann::json::array({
        {{"frame", 0}, {"camera", {{"fov_deg", 50}}}},
        {{"frame", 0}, {"camera", {{"fov_deg", 60}}}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_samples));
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

TEST_CASE("V3 render-plan decoder accepts concrete and generated path shapes") {
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
                   {"stroke", {{"color", "#FFFFFF"}, {"width", 2.0},
                               {"cap", "round"}, {"join", "bevel"},
                               {"taper_start", 0.2}, {"taper_end", 0.8}}}},
         render_plan::ShapeContentTypePlan::Line},
        {"path", {{"type", "path"},
                   {"path", {{{"type", "move_to"}, {"point", {0, 0}}},
                             {{"type", "quadratic_to"}, {"control1", {10, 20}}, {"point", {20, 0}}},
                             {{"type", "cubic_to"}, {"control1", {25, -5}}, {"control2", {35, 5}}, {"point", {40, 0}}},
                             {{"type", "close"}}}}},
         render_plan::ShapeContentTypePlan::Path},
        {"star", {{"type", "star"}, {"points", 7}, {"inner_radius_ratio", 0.4}},
         render_plan::ShapeContentTypePlan::Star},
        {"polygon", {{"type", "polygon"}, {"points", 6}, {"rotation_degrees", 15.0}},
         render_plan::ShapeContentTypePlan::Polygon},
        {"arrow", {{"type", "arrow"}, {"shaft_ratio", 0.3}, {"head_ratio", 0.8}},
         render_plan::ShapeContentTypePlan::Arrow},
        {"arc", {{"type", "arc"}, {"start_degrees", 10.0}, {"sweep_degrees", 180.0},
                  {"stroke", {{"color", "#FFFFFF"}, {"width", 2.0}}}},
         render_plan::ShapeContentTypePlan::Arc},
        {"grid", {{"type", "grid"}, {"spacing", 10.0},
                   {"stroke", {{"color", "#FFFFFF"}, {"width", 1.0}}}},
         render_plan::ShapeContentTypePlan::Grid},
        {"dot_grid", {{"type", "dot_grid"}, {"spacing", 10.0}, {"dot_radius", 2.0},
                       {"fill", {1.0, 0.0, 0.0, 1.0}}},
         render_plan::ShapeContentTypePlan::DotGrid},
        {"device_frame", {{"type", "device_frame"}, {"device", "browser"},
                           {"chrome", true}, {"fill", {0.1, 0.2, 0.3, 1.0}}},
         render_plan::ShapeContentTypePlan::DeviceFrame}
    };
    for (const auto& test : cases) {
        INFO("shape content: " << std::string{test.name});
        auto layer = render_plan_test::shape_layer(test.shape);
        auto input = render_plan_test::base_plan({layer});
        const auto decoded = render_plan::decode_render_plan(input);
        if (!decoded) INFO(decoded.error().message);
        const std::string decode_error = decoded.has_value() ? std::string{} : decoded.error().message;
        REQUIRE_MESSAGE(decoded.has_value(), decode_error);
        REQUIRE(decoded->layers.size() == 1);
        REQUIRE(decoded->layers[0].shape);
        CHECK(decoded->layers[0].shape->type == test.expected);
        if (test.expected == render_plan::ShapeContentTypePlan::Star ||
            test.expected == render_plan::ShapeContentTypePlan::Polygon ||
            test.expected == render_plan::ShapeContentTypePlan::Arrow ||
            test.expected == render_plan::ShapeContentTypePlan::Arc ||
            test.expected == render_plan::ShapeContentTypePlan::Grid ||
            test.expected == render_plan::ShapeContentTypePlan::DotGrid)
            CHECK(decoded->layers[0].shape->path.size() >= 3);
        if (test.expected == render_plan::ShapeContentTypePlan::RoundedRect)
            CHECK(decoded->layers[0].shape->radius == doctest::Approx(8.0f));
        if (test.expected == render_plan::ShapeContentTypePlan::Rect)
            CHECK(decoded->layers[0].shape->fill_color->at(0) == doctest::Approx(0.2f));
        if (test.expected == render_plan::ShapeContentTypePlan::Line) {
            REQUIRE(decoded->layers[0].shape->stroke);
            CHECK(decoded->layers[0].shape->stroke->width == doctest::Approx(2.0f));
            CHECK(decoded->layers[0].shape->stroke->cap == "round");
            CHECK(decoded->layers[0].shape->stroke->join == "bevel");
            CHECK(decoded->layers[0].shape->stroke->taper_start == doctest::Approx(0.2f));
            CHECK(decoded->layers[0].shape->stroke->taper_end == doctest::Approx(0.8f));
        }
    }

    auto unsupported = render_plan_test::shape_layer({{"type", "hexagon"}});
    const auto unknown = render_plan::decode_render_plan(
        render_plan_test::base_plan({unsupported}));
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().message.find("layers[0].shape.type") != std::string::npos);
    CHECK(unknown.error().message.find("required enum") != std::string::npos);

    auto unbounded_grid = render_plan_test::shape_layer({{"type", "grid"}, {"spacing", 0.0001},
        {"stroke", {{"color", "#FFFFFF"}}}});
    CHECK_FALSE(render_plan::decode_render_plan(render_plan_test::base_plan({unbounded_grid})));
    auto unbounded_dots = render_plan_test::shape_layer({{"type", "dot_grid"}, {"spacing", 0.1},
        {"dot_radius", 0.02}, {"fill", {1.0, 1.0, 1.0, 1.0}}});
    CHECK_FALSE(render_plan::decode_render_plan(render_plan_test::base_plan({unbounded_dots})));
}

TEST_CASE("V3 GradientMesh fill decodes with bounded nodes and fingerprints every node") {
    using nlohmann::json;
    auto nodes = json::array({
        {{"position", {0.0, 0.0}}, {"color", {1.0, 0.0, 0.0, 1.0}}},
        {{"position", {1.0, 0.0}}, {"color", {0.0, 1.0, 0.0, 1.0}}},
        {{"position", {0.0, 1.0}}, {"color", {0.0, 0.0, 1.0, 1.0}}},
        {{"position", {1.0, 1.0}}, {"color", {1.0, 1.0, 1.0, 1.0}}}
    });
    auto input = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "rect"}, {"fill", {{"type", "mesh"}, {"mesh", {{"nodes", nodes}}}}}})});
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].shape);
    REQUIRE(decoded->layers[0].shape->gradient_mesh);
    REQUIRE(decoded->layers[0].shape->gradient_mesh->nodes.size() == 4);

    auto moved_node = input;
    moved_node["layers"][0]["shape"]["fill"]["mesh"]["nodes"][1]["position"][0] = 0.9;
    const auto moved = chronon3d::render_plan::decode_render_plan(moved_node);
    REQUIRE(moved);
    CHECK(moved->content_fingerprint != decoded->content_fingerprint);

    auto recolored_node = input;
    recolored_node["layers"][0]["shape"]["fill"]["mesh"]["nodes"][2]["color"][1] = 0.4;
    const auto recolored = chronon3d::render_plan::decode_render_plan(recolored_node);
    REQUIRE(recolored);
    CHECK(recolored->content_fingerprint != decoded->content_fingerprint);

    auto weighted_node = input;
    weighted_node["layers"][0]["shape"]["fill"]["mesh"]["nodes"][0]["weight"] = 2.0;
    const auto weighted = chronon3d::render_plan::decode_render_plan(weighted_node);
    REQUIRE(weighted);
    CHECK(weighted->content_fingerprint != decoded->content_fingerprint);

    auto invalid_position = input;
    invalid_position["layers"][0]["shape"]["fill"]["mesh"]["nodes"][0]["position"][0] = 1.1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_position));
    auto invalid_weight = input;
    invalid_weight["layers"][0]["shape"]["fill"]["mesh"]["nodes"][0]["weight"] = 0.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_weight));

    auto field_conflict = input;
    field_conflict["layers"][0]["shape"]["fill"] = {
        {"type", "mesh"}, {"mesh", {{"nodes", nodes}}},
        {"field", {{"generator", "perlin"}}}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(field_conflict));
}
