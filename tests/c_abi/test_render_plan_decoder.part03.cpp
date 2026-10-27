#include "test_render_plan_decoder_test_support.hpp"

TEST_CASE("render plan V3 animates gradient stops and rejects incompatible gradient tracks") {
    auto first = render_plan_test::gradient();
    auto second = first;
    second["start"] = {0.2, 0.0};
    second["end"] = {0.8, 0.0};
    second["color_stops"][0]["color"] = {0.0, 1.0, 0.0, 1.0};
    auto source = render_plan_test::base_plan(nlohmann::json::array({
        render_plan_test::shape_layer({{"type", "rect"}, {"fill", first}})
    }));
    source["layers"][0]["fill_gradient_animation"] = {
        {"easing", "linear"},
        {"keyframes", nlohmann::json::array({
            {{"frame", 0}, {"value", first}},
            {{"frame", 8}, {"value", second}}
        })}
    };
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = decoded ? std::string{} : decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_error);
    REQUIRE(decoded->layers[0].fill_gradient_animation.has_value());
    const auto fingerprint = decoded->content_fingerprint;
    source["layers"][0]["fill_gradient_animation"]["keyframes"][1]["value"]["start"][0] = 0.3;
    const auto changed = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed.has_value());
    CHECK(changed->content_fingerprint != fingerprint);

    auto changed_geometry = source;
    changed_geometry["layers"][0]["fill_gradient_animation"]["keyframes"][1]["value"]["end"][1] = 0.4;
    const auto changed_geometry_decoded = chronon3d::render_plan::decode_render_plan(changed_geometry);
    REQUIRE(changed_geometry_decoded.has_value());
    CHECK(changed_geometry_decoded->content_fingerprint != changed->content_fingerprint);
    auto changed_color = source;
    changed_color["layers"][0]["fill_gradient_animation"]["keyframes"][1]["value"]["color_stops"][0]["color"][0] = 0.4;
    const auto changed_color_decoded = chronon3d::render_plan::decode_render_plan(changed_color);
    REQUIRE(changed_color_decoded.has_value());
    CHECK(changed_color_decoded->content_fingerprint != changed->content_fingerprint);

    source["layers"][0]["fill_gradient_animation"]["keyframes"][1]["value"]["spread"] = "pad";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("V3 shape backgrounds combine animated gradients with runtime effects") {
    using nlohmann::json;
    const auto first = render_plan_test::gradient();
    auto second = first;
    second["start"] = {0.0, 0.25};
    second["end"] = {1.0, 0.75};
    second["color_stops"][0]["color"] = {0.04, 0.055, 0.10, 1.0};
    second["color_stops"][1]["color"] = {0.10, 0.14, 0.20, 1.0};

    auto layer = render_plan_test::shape_layer({{"type", "rounded_rect"},
        {"radius", 24.0}, {"fill", first}});
    layer["size"] = {1920.0, 1080.0};
    layer["effects"] = json::array({
        {{"type", "fractal_noise"},
         {"field", {{"generator", "fractal"}, {"seed", 73}, {"frequency", 0.012}}}},
        {{"type", "vignette"}, {"radius", 0.72}, {"softness", 0.35},
         {"color", {0.0, 0.0, 0.0, 0.45}}}
    });
    auto source = render_plan_test::base_plan(json::array({layer}));
    source["canvas"] = {{"width", 1920}, {"height", 1080}, {"fps_num", 24},
                        {"fps_den", 1}, {"duration_frames", 48}};
    source["layers"][0]["fill_gradient_animation"] = {
        {"easing", "linear"},
        {"keyframes", json::array({
            {{"frame", 0}, {"value", first}},
            {{"frame", 47}, {"value", second}}
        })}
    };

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string error = decoded ? std::string{} :
        decoded.error().path + ": " + decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), error);
    REQUIRE(decoded->layers.size() == 1);
    const auto& background = decoded->layers[0];
    CHECK(background.type == chronon3d::render_plan::LayerType::Shape);
    REQUIRE(background.shape.has_value());
    CHECK(background.shape->type == chronon3d::render_plan::ShapeContentTypePlan::RoundedRect);
    CHECK(background.size[0] == doctest::Approx(1920.0f));
    CHECK(background.size[1] == doctest::Approx(1080.0f));
    REQUIRE(background.fill_gradient_animation.has_value());
    REQUIRE(background.effects.size() == 2);
    CHECK(background.effects[0].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::FractalNoise);
    CHECK(background.effects[0].noise_field.has_value());
    CHECK(background.effects[1].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::Vignette);

    auto changed = source;
    changed["layers"][0]["fill_gradient_animation"]["keyframes"][1]["value"]
        ["color_stops"][0]["color"][0] = 0.2;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan.has_value());
    CHECK(decoded->content_fingerprint != changed_plan->content_fingerprint);
}

TEST_CASE("V3 rounded shape stroke can frame an image as a lower layer") {
    using nlohmann::json;
    auto border = render_plan_test::shape_layer({
        {"type", "rounded_rect"}, {"radius", 28.0},
        {"fill", {0.04, 0.055, 0.10, 1.0}},
        {"stroke", {{"color", {0.85, 0.90, 1.0, 1.0}}, {"width", 5.0},
                    {"join", "round"}}}
    });
    border["id"] = "media-border";
    border["size"] = {420.0, 280.0};
    border["position"] = {750.0, 400.0};
    auto image = json{
        {"id", "media"}, {"type", "image"}, {"asset", "media/photo.png"},
        {"size", {410.0, 270.0}}, {"position", {750.0, 400.0}}, {"radius", 22.0}
    };
    auto source = render_plan_test::base_plan(json::array({border, image}));

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string error = decoded ? std::string{} :
        decoded.error().path + ": " + decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), error);
    REQUIRE(decoded->layers.size() == 2);
    const auto& frame = decoded->layers[0];
    const auto& media = decoded->layers[1];
    CHECK(frame.id == "media-border");
    CHECK(frame.type == chronon3d::render_plan::LayerType::Shape);
    REQUIRE(frame.shape.has_value());
    CHECK(frame.shape->type == chronon3d::render_plan::ShapeContentTypePlan::RoundedRect);
    REQUIRE(frame.shape->stroke.has_value());
    CHECK(frame.shape->stroke->width == doctest::Approx(5.0f));
    CHECK(frame.shape->stroke->join == "round");
    CHECK(media.id == "media");
    CHECK(media.type == chronon3d::render_plan::LayerType::Image);
    CHECK(media.radius == doctest::Approx(22.0f));
    CHECK(frame.size[0] > media.size[0]);
    CHECK(frame.size[1] > media.size[1]);
}

TEST_CASE("render plan requires adjustment layers to own only an effect stack") {
    const auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":24,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"grain","type":"adjustment","effects":[
        {"type":"noise","amount":0.05,"size":1.5,"seed":123,"animated":true}]}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = plan.has_value() ? std::string{} :
        plan.error().path + ": " + plan.error().message;
    REQUIRE_MESSAGE(plan.has_value(), decode_error);
    REQUIRE(plan->layers.size() == 1);
    CHECK(plan->layers[0].type == chronon3d::render_plan::LayerType::Adjustment);
    REQUIRE(plan->layers[0].effects.size() == 1);
    auto empty = source;
    empty["layers"][0]["effects"] = nlohmann::json::array();
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(empty).has_value());
    const std::vector<std::pair<std::string, nlohmann::json>> forbidden_fields{
        {"asset", "asset"}, {"source", "source"}, {"text", "must not draw"},
        {"color", nlohmann::json::array({0, 0, 0, 1})}, {"shape", nlohmann::json::object()},
        {"size", nlohmann::json::array({10, 10})}, {"particle_emitter", nlohmann::json::object()},
        {"light", nlohmann::json::object()}, {"inline_mesh", nlohmann::json::object()},
        {"position", nlohmann::json::array({0, 0})}, {"rotation", nlohmann::json::array({0})},
        {"scale", nlohmann::json::array({1, 1})}, {"animation", nlohmann::json::object()},
        {"text_animators", nlohmann::json::array()}, {"spans", nlohmann::json::array()},
        {"style", nlohmann::json::object()}, {"fill_gradient_animation", nlohmann::json::object()},
        {"motion_blur", nlohmann::json::object()}, {"mask", nlohmann::json::object()},
        {"masks", nlohmann::json::array()},
        {"safe_insets", nlohmann::json::object()}, {"time_remap", nlohmann::json::object()},
        {"track_matte", nlohmann::json::object()}, {"parent", "other"},
        {"transition_in", nlohmann::json::object()}, {"transition_out", nlohmann::json::object()},
        {"enable_3d", true}, {"screen_space", true}, {"depth_role", "overlay"},
        {"depth_offset", 2}, {"radius", 2}, {"start_frame", 0}, {"duration_frames", 1},
        {"fit", "cover"}, {"blend_mode", "normal"}, {"opacity", 1}, {"loop", true}};
    for (const auto& [field, payload] : forbidden_fields) {
        auto invalid = source;
        invalid["layers"][0][field] = payload;
        INFO("forbidden adjustment-layer field: " << field);
        CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid).has_value());
    }
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

TEST_CASE("render plan V3 decodes the remaining animated text style properties") {
    auto source = v3_text_plan();
    source["layers"][0]["text_animators"] = nlohmann::json::parse(R"JSON([
        {"id":"style","selectors":[{"unit":"glyph"}],"properties":[
          {"property":"skew","keyframes":[{"frame":0,"value":0},{"frame":10,"value":20}]},
          {"property":"skew_axis","keyframes":[{"frame":0,"value":10}]},
          {"property":"anchor","keyframes":[{"frame":0,"value":[1,2,3]}]},
          {"property":"baseline_shift","keyframes":[{"frame":0,"value":0},{"frame":10,"value":8}]},
          {"property":"background_color","keyframes":[{"frame":0,"value":[0,0,0,0]},{"frame":10,"value":[1,0,0,1]}]},
          {"property":"background_padding_em","keyframes":[{"frame":0,"value":0.5},{"frame":10,"value":1}]},
          {"property":"background_radius_em","keyframes":[{"frame":0,"value":0},{"frame":10,"value":0.25}]},
          {"property":"stroke_color","keyframes":[{"frame":0,"value":[0,0,0,1]},{"frame":10,"value":[1,1,1,1]}]},
          {"property":"stroke_width","keyframes":[{"frame":0,"value":0},{"frame":10,"value":4}]}
        ]}
    ])JSON");

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) INFO(decoded.error().path << ": " << decoded.error().message);
    REQUIRE(decoded.has_value());
    const auto& properties = decoded->layers[0].text_animators[0].properties;
    REQUIRE(properties.size() == 9);
    CHECK(properties[0].property == "skew");
    CHECK(properties[2].keyframes.back().value == std::vector<float>{1.0f, 2.0f, 3.0f});
    CHECK(properties[4].keyframes.back().value == std::vector<float>{1.0f, 0.0f, 0.0f, 1.0f});
}

TEST_CASE("render plan V3 accepts bounded negative text tracking animation") {
    auto source = v3_text_plan();
    source["layers"][0]["text_animators"] = nlohmann::json::parse(R"JSON([
        {"id":"tracking","selectors":[{"unit":"glyph"}],"properties":[
          {"property":"tracking","keyframes":[
            {"frame":0,"value":-10},{"frame":24,"value":8},{"frame":42,"value":2}
          ]}
        ]}
    ])JSON");

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) INFO(decoded.error().path << ": " << decoded.error().message);
    REQUIRE(decoded.has_value());
    const auto& track = decoded->layers[0].text_animators[0].properties[0];
    CHECK(track.property == "tracking");
    CHECK(track.keyframes.front().value == std::vector<float>{-10.0f});

    source["layers"][0]["text_animators"][0]["properties"][0]["keyframes"][0]["value"] = -1001;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
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
            "properties": [{"property": "wobble", "keyframes": [{"frame": 0, "value": 5.0}]}]
        }
    ])JSON");

    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan decodes animated dash offsets and effect time controls into fingerprints") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"path","type":"shape","size":[64,64],"shape":{
        "type":"path","path":[{"type":"move_to","point":[0,0]},
          {"type":"line_to","point":[32,0]}],
        "stroke":{"width":2,"dash_array":[5,2,1],"dash_offset":0,
          "color":"#FFFFFF"}},
        "animation":{"tracks":[{"property":"stroke_dash_offset","keyframes":[
          {"frame":0,"value":0},{"frame":29,"value":12}]}]},
        "effects":[{"type":"noise","amount":0.1,"size":1,"seed":4,
          "animated":true,"time":{"posterize_fps":8,"curve":[
            {"frame":0,"value":0},{"frame":29,"value":1}],"easing":"out_cubic"}}]}],
      "output":{"path":"out.png"}})JSON");
    const auto plan=chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(plan.has_value());
    REQUIRE(plan->layers[0].shape->stroke.has_value());
    CHECK(plan->layers[0].shape->stroke->dash_array==std::vector<float>{5,2,1});
    CHECK(plan->layers[0].effects[0].posterize_fps==doctest::Approx(8.0f));
    REQUIRE(plan->layers[0].effects[0].time_curve.has_value());
    const auto offset=chronon3d::render_plan::detail::stroke_dash_offset_at(
        plan->layers[0], 15.0, chronon3d::FrameRate{30, 1});
    REQUIRE(offset.has_value());
    CHECK(*offset>0.0f);
    auto changed=source;
    changed["layers"][0]["shape"]["stroke"]["dash_array"][0]=6;
    auto changed_plan=chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan.has_value());
    CHECK(plan->content_fingerprint!=changed_plan->content_fingerprint);
    changed=source;
    changed["layers"][0]["effects"][0]["time"]["posterize_fps"]=12;
    changed_plan=chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan.has_value());
    CHECK(plan->content_fingerprint!=changed_plan->content_fingerprint);
}

TEST_CASE("per-frame paint tracks compile into a scene instead of aborting") {
    // Regression: the whole per-frame paint family (stroke_dash_offset,
    // stroke_width, stroke_color, fill_color, shadow_<n>_*) decoded and
    // validated, then made scene compilation throw "unsupported primitive
    // animation property". A contract-valid dashed write-on was therefore
    // unrenderable, and decoding alone never caught it: the previous test only
    // evaluated stroke_dash_offset_at() against the decoded plan.
    const auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[
        {"id":"ribbon","type":"shape","size":[64,64],
         "shape":{"type":"path",
           "path":[{"type":"move_to","point":[0,0]},{"type":"line_to","point":[64,0]}],
           "stroke":{"width":2,"dash_array":[5,2,1],"dash_offset":0,"color":"#FFFFFF"}},
         "style":{"shadows":[{"color":"#000000","opacity":0.5,"blur":2,"offset":[1,1]}]},
         "animation":{"tracks":[
           {"property":"stroke_dash_offset","keyframes":[{"frame":0,"value":0},{"frame":29,"value":12}]},
           {"property":"stroke_width","keyframes":[{"frame":0,"value":1},{"frame":29,"value":6}]},
           {"property":"stroke_color","keyframes":[{"frame":0,"value":[1,1,1,1]},{"frame":29,"value":[1,0,0,1]}]},
           {"property":"shadow_0_opacity","keyframes":[{"frame":0,"value":0},{"frame":29,"value":0.5}]},
           {"property":"shadow_0_offset_x","keyframes":[{"frame":0,"value":0},{"frame":29,"value":8}]}
         ]}},
        {"id":"card","type":"shape","size":[32,32],"position":[32,32],
         "shape":{"type":"rect","fill":[0,0,1,1]},
         "animation":{"tracks":[
           {"property":"fill_color","keyframes":[{"frame":0,"value":[0,0,1,1]},{"frame":29,"value":[0,1,1,1]}]}
         ]}}
      ],
      "output":{"path":"out.png"}})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->layers.size() == 2);
    // A single build: SceneBuilder::build() consumes the builder, so calling it
    // twice would yield an empty scene and mask the assertion below.
    try {
        chronon3d::SceneBuilder scene;
        for (const auto& layer : decoded->layers) {
            scene.layer(layer.id, [&](chronon3d::LayerBuilder& builder) {
                chronon3d::render_plan::detail::apply_layer_primitives(
                    builder, layer, 64, 64, decoded->canvas.fps);
            });
        }
        const auto materialized = scene.build();
        CHECK(materialized.layers().size() == 2);
    } catch (const std::exception& error) {
        FAIL("paint-track plan failed to compile into a scene: " << error.what());
    }

    // The pass must stay strict: a property outside both the transform and the
    // paint vocabulary is still rejected rather than silently accepted. The
    // tracks are hand-built so the guard is exercised directly, since the
    // decoder would have refused these before compilation.
    const auto expect_rejected = [&](std::string property) {
        auto strict = decoded->layers.front();
        chronon3d::render_plan::AnimationTrackPlan track;
        track.property = std::move(property);
        track.keyframes = {{chronon3d::Frame{0}, {0.0f}}, {chronon3d::Frame{29}, {1.0f}}};
        strict.animation = chronon3d::render_plan::AnimationPlan{};
        strict.animation->tracks.push_back(std::move(track));
        chronon3d::SceneBuilder strict_scene;
        bool threw = false;
        try {
            strict_scene.layer(strict.id, [&](chronon3d::LayerBuilder& builder) {
                chronon3d::render_plan::detail::apply_layer_primitives(
                    builder, strict, 64, 64, decoded->canvas.fps);
            });
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    };
    expect_rejected("shear");
    expect_rejected("shadow_3_blur");
    expect_rejected("shadow_0_scale");
}

TEST_CASE("render plan accepts resolver-backed SVG geometry references") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"vector","type":"shape","size":[64,64],
        "shape":{"type":"path","asset":"art/vector.svg"}}],
      "output":{"path":"out.png"}})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->layers[0].shape.has_value());
    CHECK(decoded->layers[0].shape->svg_asset == "art/vector.svg");
    CHECK(decoded->layers[0].shape->path.empty());

    auto both_sources = source;
    both_sources["layers"][0]["shape"]["path"] = {{{"type", "move_to"}, {"point", {0, 0}}}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(both_sources).has_value());
    auto invalid_extension = source;
    invalid_extension["layers"][0]["shape"]["asset"] = "art/vector.png";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_extension).has_value());
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
            {"type":"close"}]]}},
          {"kind":"trim","params":{"start":0.25,"end":0.75,"offset":0.1,
            "animation":{"keyframes":[{"frame":0,"value":[0.25,0.25,0]},
                                         {"frame":10,"value":[0.25,0.75,0.1]}]}}}]},
      "mask":{"type":"path","path":[{"type":"move_to","point":[-4,-4]},
        {"type":"line_to","point":[4,-4]},{"type":"line_to","point":[4,4]},
        {"type":"line_to","point":[-4,4]},{"type":"close"}],
        "target_path":[{"type":"move_to","point":[-8,-8]},
        {"type":"line_to","point":[8,-8]},{"type":"line_to","point":[8,8]},
        {"type":"line_to","point":[-8,8]},{"type":"close"}],
        "animation":{"keyframes":[{"frame":0,"value":0},{"frame":12,"value":1}]},
        "feather":2,"expansion":1}}],
      "output":{"path":"out.png"}
    })JSON");
    auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = decoded.has_value() ? std::string{} : decoded.error().message;
    INFO(decode_error);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->layers[0].shape->operators.size() == 9);
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
    CHECK(decoded->layers[0].shape->operators[8].kind ==
          chronon3d::render_plan::PathOperatorKindPlan::Trim);
    CHECK(decoded->layers[0].shape->operators[8].trim_start == doctest::Approx(0.25f));
    CHECK(decoded->layers[0].shape->operators[8].trim_end == doctest::Approx(0.75f));
    CHECK(decoded->layers[0].shape->operators[8].trim_offset == doctest::Approx(0.1f));
    REQUIRE(decoded->layers[0].shape->operators[8].trim_animation.has_value());
    CHECK(decoded->layers[0].shape->operators[8].trim_animation->keyframes.size() == 2);
    REQUIRE(decoded->layers[0].path_mask.has_value());
    CHECK(decoded->layers[0].path_mask->feather == doctest::Approx(2));
    CHECK(decoded->layers[0].path_mask->path.size() == 5);
    REQUIRE(decoded->layers[0].path_mask->morph_animation.has_value());
    CHECK(decoded->layers[0].path_mask->morph_target.size() == 5);
    CHECK(decoded->layers[0].path_mask->morph_animation->keyframes.size() == 2);
    const auto mask_fingerprint = decoded->content_fingerprint;
    auto changed_mask = source;
    changed_mask["layers"][0]["mask"]["target_path"][1]["point"][0] = 12;
    const auto changed_mask_decoded = chronon3d::render_plan::decode_render_plan(changed_mask);
    REQUIRE(changed_mask_decoded.has_value());
    CHECK(changed_mask_decoded->content_fingerprint != mask_fingerprint);
    source["layers"][0]["shape"]["operators"][8]["kind"] = "future_operator";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan decoder accepts BooleanUnion as distinct from path concatenation") {
    const auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"boolean-path","type":"shape","size":[64,64],"shape":{
        "type":"path","path":[{"type":"move_to","point":[0,0]},
          {"type":"line_to","point":[10,0]},{"type":"line_to","point":[10,10]},
          {"type":"line_to","point":[0,10]},{"type":"close"}],
        "operators":[{"kind":"boolean_union","params":{"paths":[[
          {"type":"move_to","point":[5,5]},
          {"type":"line_to","point":[15,5]},
          {"type":"line_to","point":[15,15]},
          {"type":"line_to","point":[5,15]},
          {"type":"close"}]]}}]} }],
      "output":{"path":"out.png"}
    })JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->layers[0].shape->operators.size() == 1);
    CHECK(decoded->layers[0].shape->operators[0].kind ==
          chronon3d::render_plan::PathOperatorKindPlan::BooleanUnion);
    CHECK(decoded->layers[0].shape->operators[0].paths.size() == 1);
}

TEST_CASE("render plan validates matte dependency cycles before lowering") {
    const auto layer = [](const char* id, const char* source) {
        nlohmann::json value{{"id", id}, {"type", "color"}, {"color", {1, 1, 1, 1}}};
        if (source)
            value["track_matte"] = {{"source", source}, {"type", "alpha"}};
        return value;
    };
    const auto valid = render_plan_test::base_plan(
        nlohmann::json::array({layer("content", "matte"), layer("matte", nullptr)}));
    const auto valid_result = chronon3d::render_plan::decode_render_plan(valid);
    REQUIRE(valid_result.has_value());
    CHECK(valid_result->layers[0].track_matte->type ==
          chronon3d::render_plan::TrackMatteTypePlan::Alpha);

    const auto missing = render_plan_test::base_plan(
        nlohmann::json::array({layer("content", "not-present")}));
    const auto missing_result = chronon3d::render_plan::decode_render_plan(missing);
    REQUIRE_FALSE(missing_result.has_value());
    CHECK(missing_result.error().code == "track_matte_source_missing");

    const auto cyclic = render_plan_test::base_plan(
        nlohmann::json::array({layer("a", "b"), layer("b", "a")}));
    const auto cyclic_result = chronon3d::render_plan::decode_render_plan(cyclic);
    REQUIRE_FALSE(cyclic_result.has_value());
    CHECK(cyclic_result.error().code == "layer_dependency_cycle");
}

TEST_CASE("render plan decodes and fingerprints all alpha and luma matte modes") {
    const std::array<std::pair<const char*, chronon3d::render_plan::TrackMatteTypePlan>, 4> modes{{
        {"alpha", chronon3d::render_plan::TrackMatteTypePlan::Alpha},
        {"alpha_inverted", chronon3d::render_plan::TrackMatteTypePlan::AlphaInverted},
        {"luma", chronon3d::render_plan::TrackMatteTypePlan::Luma},
        {"luma_inverted", chronon3d::render_plan::TrackMatteTypePlan::LumaInverted}}};
    std::optional<std::uint64_t> previous;
    for (const auto& [name, expected] : modes) {
        auto source = render_plan_test::base_plan(nlohmann::json::array({
            { {"id", "content"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
              {"track_matte", {{"source", "matte"}, {"type", name}}} },
            { {"id", "matte"}, {"type", "color"}, {"color", {1, 1, 1, 1}} }}));
        const auto decoded = chronon3d::render_plan::decode_render_plan(source);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->layers[0].track_matte.has_value());
        CHECK(decoded->layers[0].track_matte->type == expected);
        if (previous) CHECK(decoded->content_fingerprint != *previous);
        previous = decoded->content_fingerprint;
    }
}

TEST_CASE("render plan decodes bounded ordered mask stacks and fingerprints every operation") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({{
        {"id", "masked"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"masks", nlohmann::json::array({
            {{"type", "rect"}, {"mode", "add"}, {"size", {80, 60}}, {"position", {2, 3}},
             {"feather", 2.0}, {"feather_track", {
                 {"keyframes", {{{"frame", 0}, {"value", 2.0}},
                                 {{"frame", 10}, {"value", 6.0}}}}}},
             {"opacity", 1.0}, {"opacity_track", {
                 {"keyframes", {{{"frame", 0}, {"value", 0.0}},
                                 {{"frame", 10}, {"value", 1.0}}}}}}},
            {{"type", "ellipse"}, {"mode", "intersect"}, {"size", {48, 32}}, {"inverted", true}},
            {{"type", "rounded_rect"}, {"mode", "subtract"}, {"size", {20, 18}}, {"radius", 4}},
            {{"type", "path"}, {"mode", "difference"},
             {"path", {{{"type", "move_to"}, {"point", {-4, -4}}},
                       {{"type", "line_to"}, {"point", {4, -4}}},
                       {{"type", "line_to"}, {"point", {0, 4}}},
                       {{"type", "close"}}}}}
        })}
    }}));
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = decoded.has_value() ? std::string{} :
        decoded.error().path + ": " + decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_error);
    REQUIRE(decoded->layers.size() == 1);
    REQUIRE(decoded->layers[0].masks.size() == 4);
    CHECK(decoded->layers[0].masks[0].type == "rect");
    CHECK(decoded->layers[0].masks[1].mode == "intersect");
    CHECK(decoded->layers[0].masks[1].inverted);
    CHECK(decoded->layers[0].masks[2].mode == "subtract");
    CHECK(decoded->layers[0].masks[3].mode == "difference");
    REQUIRE(decoded->layers[0].masks[0].feather_track);
    REQUIRE(decoded->layers[0].masks[0].opacity_track);
    CHECK(chronon3d::render_plan::detail::evaluate_scalar_track(
        *decoded->layers[0].masks[0].feather_track, 5.0, 2.0f,
        decoded->canvas.fps) == doctest::Approx(4.0f));
    CHECK(chronon3d::render_plan::detail::evaluate_scalar_track(
        *decoded->layers[0].masks[0].opacity_track, 5.0, 1.0f,
        decoded->canvas.fps) == doctest::Approx(0.5f));
    const auto fingerprint = decoded->content_fingerprint;

    auto changed_track = source;
    changed_track["layers"][0]["masks"][0]["opacity_track"]["keyframes"][1]["value"] = 0.8;
    const auto changed_track_plan = chronon3d::render_plan::decode_render_plan(changed_track);
    REQUIRE(changed_track_plan.has_value());
    CHECK(changed_track_plan->content_fingerprint != fingerprint);

    auto invalid_track = source;
    invalid_track["layers"][0]["masks"][0]["opacity_track"]["property"] = "position";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_track).has_value());
    invalid_track = source;
    invalid_track["layers"][0]["masks"][0]["opacity_track"]["keyframes"][1]["value"] = 1.2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_track).has_value());

    source["layers"][0]["masks"][2]["mode"] = "add";
    const auto changed = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed.has_value());
    CHECK(changed->content_fingerprint != fingerprint);

    source["layers"][0]["masks"][0]["mode"] = "subtract";
    const auto leading_subtract = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(leading_subtract.has_value());
    CHECK(leading_subtract->content_fingerprint != fingerprint);
    source["layers"][0].erase("masks");
    source["layers"][0]["masks"] = nlohmann::json::array({
        {{"type", "ellipse"}, {"mode", "add"}, {"size", {-2, 4}}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
    source["layers"][0]["masks"] = nlohmann::json::array();
    for (int index = 0; index < 17; ++index)
        source["layers"][0]["masks"].push_back(
            {{"type", "rect"}, {"mode", "add"}, {"size", {2, 2}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan V3 decodes a bounded spring segment and its config") {
    const auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"spring","type":"shape","size":[8,8],"shape":{"type":"rect"},
        "animation":{"tracks":[{"property":"position_y","easing":"spring",
          "spring":{"mass":1,"stiffness":300,"damping":12,"initial_velocity":0},
          "keyframes":[{"frame":0,"value":-300},{"frame":10,"value":0}]}]}}],
      "output":{"path":"out.png"}
    })JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) MESSAGE("spring segment plan decode failed: " << decoded.error().message);
    REQUIRE(decoded);
    const auto& track = decoded->layers[0].animation->tracks[0];
    CHECK(track.easing == "spring");
    REQUIRE(track.spring.has_value());
    CHECK(track.spring->stiffness == doctest::Approx(300.0f));
    auto invalid_config = source;
    invalid_config["layers"][0]["animation"]["tracks"][0]["spring"]["mass"] = 0.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_config));
    auto zero_span = source;
    zero_span["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["frame"] = 0;
    const auto rejected_span = chronon3d::render_plan::decode_render_plan(zero_span);
    CHECK_FALSE(rejected_span);
    if (!rejected_span)
        CHECK(rejected_span.error().message.find("spring segment requires positive frame span") !=
              std::string::npos);
}

TEST_CASE("render plan V3 rejects unsupported world maps, charts, and shape counters") {
    const auto check_rejected = [](nlohmann::json shape) {
        auto source = render_plan_test::base_plan({
            render_plan_test::shape_layer(std::move(shape))});
        const auto decoded = chronon3d::render_plan::decode_render_plan(source);
        CHECK_FALSE(decoded);
    };

    // These are structurally meaningful payloads, not just missing-data cases:
    // the contract must reject them at schema/decode time rather than accept a
    // shape that one compiler path renders as a blank base rectangle.
    check_rejected({{"type", "world_map"}, {"world_map", {
        {"projection", "mercator"}, {"center", {12.0, 41.0}}, {"zoom", 3.0},
        {"route", {{-74.0, 40.7}, {12.5, 41.9}}}, {"highlight", {"IT"}}}}});
    check_rejected({{"type", "chart"}, {"chart", {
        {"type", "pie"}, {"data", {2.0, 3.0}}, {"donut", true},
        {"donut_inner_ratio", 0.5}, {"value_format", "currency"}}}});
    check_rejected({{"type", "rect"}, {"counter", {{"from", 0}, {"to", 100}}}});
    check_rejected({{"type", "rect"}, {"world_map", {{"route", {{0, 0}, {1, 1}}}}}});
    check_rejected({{"type", "rect"}, {"chart", {{"type", "bar"}, {"data", {1, 2}}}}});
}

TEST_CASE("render plan V3 retains the implemented text_counter contract") {
    nlohmann::json counter_layer = nlohmann::json::parse(R"JSON({
      "id":"counter","type":"text","text":"Revenue: {{value}}",
      "style":{"font":"fonts/roboto.ttf","font_size":24,"fill":"#FFFFFF"},
      "text_counter":{"token":"{{value}}","counter":{"from":0,"to":100,
        "duration_frames":12,"format":"currency"}}
    })JSON");
    auto source = render_plan_test::base_plan({std::move(counter_layer)});
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = decoded.has_value() ? std::string{} :
        decoded.error().path + ": " + decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_error);
    REQUIRE(decoded->layers[0].text_counter);
    CHECK(decoded->layers[0].text_counter->counter.to == doctest::Approx(100.0f));
    CHECK(decoded->layers[0].text_counter->counter.format == "currency");
}
