#include "test_render_plan_decoder_test_support.hpp"

TEST_CASE("V3 flow and grid layouts decode with bounded contracts and fingerprint inputs") {
    auto source=nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":30,"fps_den":1,"duration_frames":24},
      "layers":[{"id":"card","type":"color","color":[1,1,1,1],"flow":{"group_id":"cards","axis":"row","gap":8,"cell_size":[120,80],"justify":"space_between"}}],
      "output":{"path":"out.png"}
    })JSON");
    const auto decoded=chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) MESSAGE("flow/grid decode failed: " << decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].flow_layout);
    CHECK(decoded->layers[0].flow_layout->group_id=="cards");
    auto changed=source; changed["layers"][0]["flow"]["gap"]=16;
    const auto other=chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(other);
    CHECK(decoded->content_fingerprint!=other->content_fingerprint);
    auto invalid=source; invalid["layers"][0]["grid_layout"]={{"group_id","cards"},{"columns",0}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));
}

TEST_CASE("V3 roughen edges effect decodes, fingerprints, and rejects invalid bounds") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({
        render_plan_test::shape_layer({{"type", "rect"}})
    }));
    source["layers"][0]["effects"] = nlohmann::json::array({{
        {"type", "roughen_edges"}, {"amount", 3.0}, {"scale", 6.0},
        {"complexity", 3}, {"evolution", 1.25}, {"evolution_speed", 0.5},
        {"seed", 17}
    }});
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 1);
    const auto& effect = decoded->layers[0].effects[0];
    CHECK(effect.kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::RoughenEdges);
    CHECK(effect.amount == doctest::Approx(3.0f));
    CHECK(effect.roughen_scale == doctest::Approx(6.0f));
    CHECK(effect.roughen_complexity == 3);
    CHECK(effect.roughen_seed == 17);
    auto changed = source;
    changed["layers"][0]["effects"][0]["seed"] = 18;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(changed_plan->content_fingerprint != decoded->content_fingerprint);
    for (const auto& invalid_value : {nlohmann::json(65.0), nlohmann::json(-0.1)}) {
        auto invalid = source;
        invalid["layers"][0]["effects"][0]["amount"] = invalid_value;
        CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));
    }
    auto invalid_complexity = source;
    invalid_complexity["layers"][0]["effects"][0]["complexity"] = 5;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_complexity));
}

TEST_CASE("V3 rejects camera shake authored outside ChrononMotion3D") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "camera-layer"}, {"type", "color"}, {"color", {0, 0, 0, 1}}}
    }));
    source["camera_shake"] = {{"amplitude_px", 12.0}, {"frequency_hz", 6.0},
        {"decay", 0.4}, {"seed", 19}, {"rotation_amplitude_deg", 1.5},
        {"start_frame", 3}, {"duration_frames", 20}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
}

TEST_CASE("V3 effect parameter tracks validate catalog parameters and affect fingerprints") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({
        render_plan_test::shape_layer({{"type", "rect"}})
    }));
    source["layers"][0]["effects"] = nlohmann::json::array({
        {{"type", "gaussian_blur"}, {"radius", 0.0}}
    });
    source["layers"][0]["effect_param_tracks"] = nlohmann::json::array({{
        {"effect_id", "blur.gaussian"}, {"param", "radius"},
        {"keyframes", {{{"frame", 0}, {"value", 0.0}}, {{"frame", 10}, {"value", 100.0}}}}
    }});
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effect_param_tracks.size() == 1);
    auto changed = source;
    changed["layers"][0]["effect_param_tracks"][0]["keyframes"][1]["value"] = 120.0;
    const auto other = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(other);
    CHECK(decoded->content_fingerprint != other->content_fingerprint);

    auto invalid = source;
    invalid["layers"][0]["effect_param_tracks"][0]["param"] = "unknown";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));
    invalid = source;
    invalid["layers"][0]["effect_param_tracks"][0]["effect_id"] = "blur.unknown";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));
    invalid = source;
    invalid["layers"][0]["effect_param_tracks"][0]["keyframes"][1]["value"] = 300.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));

    source["layers"][0]["effects"] = nlohmann::json::array({nlohmann::json{
        {"type", "catalog"}, {"effect_id", "color.tint"},
        {"params", nlohmann::json::array({nlohmann::json{{"param", "amount"}, {"value", 0.25}}})}
    }});
    source["layers"][0]["effect_param_tracks"][0]["effect_id"] = "color.tint";
    source["layers"][0]["effect_param_tracks"][0]["param"] = "amount";
    source["layers"][0]["effect_param_tracks"][0]["keyframes"][1]["value"] = 0.75;
    const auto catalog_effect = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(catalog_effect);
    CHECK(catalog_effect->layers[0].effects[0].kind ==
          chronon3d::render_plan::LayerPlan::EffectKindPlan::Catalog);
    auto invalid_catalog_param = source;
    invalid_catalog_param["layers"][0]["effects"][0]["params"][0]["param"] = "not_animatable";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_catalog_param));
}

TEST_CASE("effect parameter setters round animated integer parameters deterministically") {
    chronon3d::MosaicParams mosaic;
    CHECK(chronon3d::effects::set_animatable_effect_param(
        mosaic, chronon3d::effects::ids::StylizeMosaic, "block_size", 3.7f));
    CHECK(mosaic.block_size == 4);
}

TEST_CASE("premium effect catalog entries expose working scalar animation parameters") {
    using namespace chronon3d::effects;
    constexpr std::array<std::string_view, 4> effect_ids{
        ids::DistortLensDistortion, ids::LightLuminanceResponse,
        ids::DistortFoldWarp, ids::LightCreaseShading};
    for (const auto effect_id : effect_ids) {
        auto params = make_default_animatable_effect_params(effect_id);
        REQUIRE(params);
        std::size_t rows_for_effect = 0;
        for (const auto& definition : kAnimatableEffectParams) {
            if (definition.effect_id != effect_id) continue;
            ++rows_for_effect;
            const auto initial = get_animatable_effect_param(
                *params, definition.effect_id, definition.param);
            REQUIRE(initial);
            const float sample = (definition.minimum + definition.maximum) * 0.5f;
            CHECK(set_animatable_effect_param(
                *params, definition.effect_id, definition.param, sample));
            const auto updated = get_animatable_effect_param(
                *params, definition.effect_id, definition.param);
            REQUIRE(updated);
            CHECK(*updated == doctest::Approx(sample));
        }
        CHECK(rows_for_effect > 0);
    }
}

TEST_CASE("V3 inline data feeds validate scalar samples and affect plan identity") {
    auto source = render_plan_test::base_plan({nlohmann::json{
        {"id", "fed"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"animation", {{"tracks", nlohmann::json::array({nlohmann::json{
            {"property", "opacity"},
            {"data_feed", {{"values", {0.0, 0.5, 1.0}}, {"interpolation", "linear"}}}
        }})}}}
    }});
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].animation);
    REQUIRE(decoded->layers[0].animation->tracks[0].data_feed);
    CHECK(decoded->layers[0].animation->tracks[0].keyframes.size() == 1);
    auto changed = source;
    changed["layers"][0]["animation"]["tracks"][0]["data_feed"]["values"][1] = 0.6;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(decoded->content_fingerprint != changed_plan->content_fingerprint);
    auto invalid = source;
    invalid["layers"][0]["animation"]["tracks"][0]["data_feed"]["unexpected"] = true;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));
}

TEST_CASE("V3 audio_react declarations are strict and included in plan identity") {
    auto source = render_plan_test::base_plan({nlohmann::json{
        {"id", "audio-reactive"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"animation", {{"tracks", nlohmann::json::array({nlohmann::json{
            {"property", "opacity"},
            {"audio_react", {{"source", "mix:0"}, {"feature", "rms"},
                {"band_hz", {100.0, 4000.0}}, {"smoothing_frames", 3},
                {"gain", 2.0}, {"map", "linear"}}}
        }})}}}
    }});
    source["audio"] = nlohmann::json{{"clips", nlohmann::json::array({
        nlohmann::json{{"id", "beat"}, {"asset", "beat.wav"}}})}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    const auto& driver = *decoded->layers[0].animation->tracks[0].audio_react;
    CHECK(driver.source == "mix:0");
    CHECK(driver.band_hz->at(0) == doctest::Approx(100.0f));
    CHECK(driver.smoothing_frames == 3);
    auto changed = source;
    changed["layers"][0]["animation"]["tracks"][0]["audio_react"]["gain"] = 2.5;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(decoded->content_fingerprint != changed_plan->content_fingerprint);
    auto invalid = source;
    invalid["layers"][0]["animation"]["tracks"][0]["audio_react"]["unexpected"] = true;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));
    invalid = source;
    invalid["layers"][0]["animation"]["tracks"][0]["audio_react"]["band_hz"] = {4000.0, 100.0};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));
}

TEST_CASE("V3 scalar procedural drivers validate strictly and affect plan fingerprints") {
    auto source = render_plan_test::base_plan({{
        {"id", "animated"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
        {"animation", {{"tracks", nlohmann::json::array({{
            {"property", "opacity"},
            {"keyframes", {{{"frame", 0}, {"value", 0.0}},
                            {{"frame", 12}, {"value", 1.0}}}},
            {"oscillate", {{"frequency_hz", 2.0}, {"amplitude", 0.5},
                            {"waveform", "triangle"}, {"phase_deg", 30.0},
                            {"offset", 0.25}}}
        }})}}}
    }});
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].animation);
    REQUIRE(decoded->layers[0].animation->tracks.size() == 1);
    const auto& track = decoded->layers[0].animation->tracks[0];
    REQUIRE(track.oscillate);
    CHECK(track.oscillate->waveform == "triangle");
    const auto repeat = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeat);
    CHECK(decoded->content_fingerprint == repeat->content_fingerprint);

    auto changed = source;
    changed["layers"][0]["animation"]["tracks"][0]["oscillate"]["frequency_hz"] = 3.0;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(decoded->content_fingerprint != changed_plan->content_fingerprint);

    auto invalid_frequency = source;
    invalid_frequency["layers"][0]["animation"]["tracks"][0]["oscillate"]["frequency_hz"] = 0.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_frequency));
    auto invalid_interval = source;
    auto& track_json = invalid_interval["layers"][0]["animation"]["tracks"][0];
    track_json.erase("oscillate");
    track_json["random_hold"] = {{"interval_frames", 0}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_interval));
    auto non_scalar = source;
    non_scalar["layers"][0]["animation"]["tracks"][0]["property"] = "position";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(non_scalar));
    auto conflicting = source;
    conflicting["layers"][0]["animation"]["tracks"][0]["random_hold"] =
        {{"interval_frames", 2}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(conflicting));

    auto random_source = source;
    auto& random_track = random_source["layers"][0]["animation"]["tracks"][0];
    random_track.erase("oscillate");
    random_track["random_hold"] = {{"interval_frames", 4}, {"min", 0.1},
                                   {"max", 0.9}, {"seed", 17}};
    const auto random_plan = chronon3d::render_plan::decode_render_plan(random_source);
    REQUIRE(random_plan);
    random_track["random_hold"]["seed"] = 18;
    const auto changed_random = chronon3d::render_plan::decode_render_plan(random_source);
    REQUIRE(changed_random);
    CHECK(random_plan->content_fingerprint != changed_random->content_fingerprint);

    auto posterize_source = source;
    auto& posterize_track = posterize_source["layers"][0]["animation"]["tracks"][0];
    posterize_track.erase("oscillate");
    posterize_track["posterize_time"] = {{"fps", 8.0}};
    const auto posterize_plan = chronon3d::render_plan::decode_render_plan(posterize_source);
    REQUIRE(posterize_plan);
    posterize_track["posterize_time"]["fps"] = 12.0;
    const auto changed_posterize = chronon3d::render_plan::decode_render_plan(posterize_source);
    REQUIRE(changed_posterize);
    CHECK(posterize_plan->content_fingerprint != changed_posterize->content_fingerprint);
}

TEST_CASE("scalar procedural animation drivers are frame-deterministic") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    const FrameRate fps{30, 1};
    CHECK(SampleTime::from_frame(15.0, fps).fps() == doctest::Approx(30.0));

    AnimatedValue<float> oscillating{0.0f};
    oscillating.key(Frame{0}, 0.0f).key(Frame{30}, 1.0f);
    ScalarAnimationDriver oscillator;
    oscillator.kind = ScalarAnimationDriver::Kind::Oscillate;
    oscillator.waveform = ScalarAnimationDriver::Waveform::Square;
    oscillator.frequency_hz = 1.0f;
    oscillator.amplitude = 2.0f;
    oscillator.offset = 1.0f;
    oscillating.scalar_driver(oscillator);
    CHECK(oscillating.evaluate(SampleTime::from_frame(0.0, fps)) == doctest::Approx(3.0f));
    CHECK(oscillating.evaluate(SampleTime::from_frame(15.0, fps)) == doctest::Approx(-1.0f));
    CHECK(oscillating.evaluate(SampleTime::from_frame(30.0, fps)) == doctest::Approx(3.0f));

    AnimatedValue<float> held{0.0f};
    ScalarAnimationDriver random_hold;
    random_hold.kind = ScalarAnimationDriver::Kind::RandomHold;
    random_hold.interval_frames = 4;
    random_hold.seed = 17;
    random_hold.min = -2.0f;
    random_hold.max = 3.0f;
    held.scalar_driver(random_hold);
    const auto bucket_value = held.evaluate(SampleTime::from_frame(1.25, fps));
    CHECK(held.evaluate(SampleTime::from_frame(3.99, fps)) == bucket_value);
    CHECK(held.evaluate(SampleTime::from_frame(1.25, fps)) == bucket_value);
    CHECK(bucket_value >= -2.0f);
    CHECK(bucket_value < 3.0f);

    AnimatedValue<float> posterized{0.0f};
    posterized.key(Frame{0}, 0.0f).key(Frame{30}, 30.0f);
    ScalarAnimationDriver posterize;
    posterize.kind = ScalarAnimationDriver::Kind::PosterizeTime;
    posterize.fps = 8.0f;
    posterized.scalar_driver(posterize);
    const auto before_step = posterized.evaluate(SampleTime::from_frame(3.74, fps));
    CHECK(posterized.evaluate(SampleTime::from_frame(0.0, fps)) == before_step);
    CHECK(posterized.evaluate(SampleTime::from_frame(3.75, fps)) > before_step);

    auto make_stroke_layer = [](AnimationTrackPlan track) {
        LayerPlan layer;
        layer.id = "procedural-stroke";
        ShapeContentPlan shape;
        shape.stroke = chronon3d::render_plan::StrokeStyle{};
        layer.shape = std::move(shape);
        layer.animation = AnimationPlan{{std::move(track)}};
        return layer;
    };
    AnimationTrackPlan stroke_oscillation;
    stroke_oscillation.property = "stroke_width";
    stroke_oscillation.keyframes = {{Frame{0}, {1.0f}}, {Frame{30}, {1.0f}}};
    AnimationTrackPlan::OscillateDriverPlan compiled_oscillator;
    compiled_oscillator.frequency_hz = 1.0f;
    compiled_oscillator.amplitude = 2.0f;
    compiled_oscillator.offset = 3.0f;
    compiled_oscillator.waveform = "square";
    stroke_oscillation.oscillate = compiled_oscillator;
    const auto compiled_oscillation_layer = make_stroke_layer(std::move(stroke_oscillation));
    CHECK(chronon3d::render_plan::detail::stroke_width_at(
        compiled_oscillation_layer, 0.0, 1.0f, fps) == doctest::Approx(5.0f));
    CHECK(chronon3d::render_plan::detail::stroke_width_at(
        compiled_oscillation_layer, 15.0, 1.0f, fps) == doctest::Approx(1.0f));

    AnimationTrackPlan stroke_random_hold;
    stroke_random_hold.property = "stroke_width";
    stroke_random_hold.keyframes = {{Frame{0}, {1.0f}}};
    stroke_random_hold.random_hold = AnimationTrackPlan::RandomHoldDriverPlan{
        .interval_frames = 4, .min = 2.0f, .max = 4.0f, .seed = 17};
    const auto compiled_random_layer = make_stroke_layer(std::move(stroke_random_hold));
    const auto random_a = chronon3d::render_plan::detail::stroke_width_at(
        compiled_random_layer, 1.0, 1.0f, fps);
    const auto random_b = chronon3d::render_plan::detail::stroke_width_at(
        compiled_random_layer, 3.99, 1.0f, fps);
    REQUIRE(random_a);
    CHECK(random_b == random_a);
    CHECK(*random_a >= 2.0f);
    CHECK(*random_a < 4.0f);

    AnimationTrackPlan stroke_posterize;
    stroke_posterize.property = "stroke_width";
    stroke_posterize.keyframes = {{Frame{0}, {0.0f}}, {Frame{30}, {30.0f}}};
    stroke_posterize.posterize_time = AnimationTrackPlan::PosterizeTimeDriverPlan{8.0f};
    const auto compiled_posterize_layer = make_stroke_layer(std::move(stroke_posterize));
    CHECK(chronon3d::render_plan::detail::stroke_width_at(
        compiled_posterize_layer, 3.74, 30.0f, fps) == doctest::Approx(0.0f));
    CHECK(chronon3d::render_plan::detail::stroke_width_at(
        compiled_posterize_layer, 3.75, 30.0f, fps) == doctest::Approx(3.75f));
}

TEST_CASE("position tracks evaluate independent x and y easing") {
    auto source = render_plan_test::base_plan({{
        {"id", "axis-easing"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
    }});
    source["layers"][0]["animation"] = nlohmann::json::parse(
        R"JSON({"tracks":[{"property":"position","keyframes":[
          {"frame":0,"value":[0,0],"easing_xy":["in_quad","out_quad"]},
          {"frame":10,"value":[10,10]}]}]})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    chronon3d::SceneBuilder scene;
    scene.layer("axis-easing", [&](chronon3d::LayerBuilder& builder) {
        chronon3d::render_plan::detail::apply_layer_primitives(
            builder, decoded->layers.front(), 64, 64, decoded->canvas.fps);
    });
    const auto materialized = scene.build();
    REQUIRE(materialized.layers().size() == 1);
    const auto position = materialized.layers().front().anim_transform.position.evaluate(
        chronon3d::SampleTime::from_frame(5.0, decoded->canvas.fps));
    CHECK(position.x == doctest::Approx(2.5f));
    CHECK(position.y == doctest::Approx(7.5f));

    source["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["easing_xy"][1] = "in_quad";
    const auto changed = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != decoded->content_fingerprint);
}

TEST_CASE("layer skew is compiled as a deterministic axis-aware shear") {
    auto source = render_plan_test::base_plan({{
        {"id", "skewed"}, {"type", "color"}, {"color", {1, 1, 1, 1}},
        {"skew", 30.0}, {"skew_axis", 90.0},
    }});
    source["layers"][0]["animation"] = nlohmann::json::parse(
        R"JSON({"tracks":[{"property":"skew","keyframes":[
          {"frame":0,"value":30},{"frame":10,"value":0}]}]})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    chronon3d::SceneBuilder scene;
    scene.layer("skewed", [&](chronon3d::LayerBuilder& builder) {
        chronon3d::render_plan::detail::apply_layer_primitives(
            builder, decoded->layers.front(), 64, 64, decoded->canvas.fps);
    });
    const auto materialized = scene.build();
    REQUIRE(materialized.layers().size() == 1);
    const auto& layer = materialized.layers().front();
    CHECK(layer.transform.skew.x == doctest::Approx(30.0f));
    CHECK(layer.anim_transform.skew.evaluate(
        chronon3d::SampleTime::from_frame(5.0, decoded->canvas.fps)).x == doctest::Approx(15.0f));
    CHECK(layer.anim_transform.skew.evaluate(
        chronon3d::SampleTime::from_frame(5.0, decoded->canvas.fps)).y == doctest::Approx(90.0f));

    chronon3d::Transform transform;
    transform.skew = {30.0f, 0.0f};
    const auto transformed = transform.to_mat4() * chronon3d::Vec4{0.0f, 1.0f, 0.0f, 1.0f};
    CHECK(transformed.x == doctest::Approx(0.57735027f));
}

TEST_CASE("flow layout stagger delays child tracks in a deterministic order") {
    using nlohmann::json;
    auto make_plan = [](const std::string& order, std::uint32_t seed = 7) {
        json layers = json::array();
        for (int i = 0; i < 5; ++i) {
            layers.push_back({
                {"id", "card-" + std::to_string(i)}, {"type", "color"},
                {"position", {static_cast<double>(4 - i) * 20.0, static_cast<double>(i) * 20.0}},
                {"color", {0.2, 0.3, 0.4, 1.0}},
                {"flow", {{"group_id", "cards"}, {"stagger", {
                    {"per_item_frames", 3}, {"order", order}, {"seed", seed},
                    {"track_property", "opacity"}}}}},
                {"animation", {{"tracks", json::array({{
                    {"property", "opacity"},
                    {"keyframes", {{{"frame", 0}, {"value", 0.0}},
                                    {{"frame", 1}, {"value", 1.0}}}}
                }})}}}
            });
        }
        auto plan = render_plan_test::base_plan(std::move(layers));
        plan["canvas"]["duration_frames"] = 100;
        return plan;
    };
    const auto forward = chronon3d::render_plan::decode_render_plan(make_plan("forward"));
    REQUIRE(forward);
    const std::array<std::int64_t, 5> forward_frames{0, 3, 6, 9, 12};
    for (std::size_t i = 0; i < forward_frames.size(); ++i)
        CHECK(forward->layers[i].animation->tracks[0].keyframes[0].frame.integral() == forward_frames[i]);

    const auto reverse = chronon3d::render_plan::decode_render_plan(make_plan("reverse"));
    REQUIRE(reverse);
    const std::array<std::int64_t, 5> reverse_frames{12, 9, 6, 3, 0};
    for (std::size_t i = 0; i < reverse_frames.size(); ++i)
        CHECK(reverse->layers[i].animation->tracks[0].keyframes[0].frame.integral() == reverse_frames[i]);

    const auto center = chronon3d::render_plan::decode_render_plan(make_plan("center_out"));
    REQUIRE(center);
    const std::array<std::int64_t, 5> center_frames{9, 3, 0, 6, 12};
    for (std::size_t i = 0; i < center_frames.size(); ++i)
        CHECK(center->layers[i].animation->tracks[0].keyframes[0].frame.integral() == center_frames[i]);

    const auto random_a = chronon3d::render_plan::decode_render_plan(make_plan("random", 77));
    const auto random_b = chronon3d::render_plan::decode_render_plan(make_plan("random", 77));
    const auto random_changed = chronon3d::render_plan::decode_render_plan(make_plan("random", 78));
    REQUIRE(random_a); REQUIRE(random_b); REQUIRE(random_changed);
    CHECK(random_a->content_fingerprint == random_b->content_fingerprint);
    CHECK(random_a->content_fingerprint != random_changed->content_fingerprint);
    for (std::size_t i = 0; i < 5; ++i)
        CHECK(random_a->layers[i].animation->tracks[0].keyframes[0].frame ==
              random_b->layers[i].animation->tracks[0].keyframes[0].frame);

    const auto spatial_x = chronon3d::render_plan::decode_render_plan(make_plan("spatial_x"));
    REQUIRE(spatial_x);
    const std::array<std::int64_t, 5> spatial_x_frames{12, 9, 6, 3, 0};
    for (std::size_t i = 0; i < spatial_x_frames.size(); ++i)
        CHECK(spatial_x->layers[i].animation->tracks[0].keyframes[0].frame.integral() == spatial_x_frames[i]);

    const auto radial = chronon3d::render_plan::decode_render_plan(make_plan("radial"));
    REQUIRE(radial);
    CHECK(radial->layers[2].animation->tracks[0].keyframes[0].frame.integral() == 0);

    auto run_level = make_plan("spatial_x");
    for (auto& layer : run_level["layers"]) {
        layer.erase("flow");
        layer["stagger_group"] = {{"group_id", "captions"}, {"per_item_frames", 2},
                                  {"order", "spatial_y"}, {"track_property", "opacity"}};
    }
    const auto run_group = chronon3d::render_plan::decode_render_plan(run_level);
    if (!run_group) {
        FAIL("run-level stagger decode failed at " << run_group.error().path << ": "
             << run_group.error().message);
        return;
    }
    const std::array<std::int64_t, 5> run_frames{0, 2, 4, 6, 8};
    for (std::size_t i = 0; i < run_frames.size(); ++i)
        CHECK(run_group->layers[i].animation->tracks[0].keyframes[0].frame.integral() == run_frames[i]);

    auto without_target = make_plan("forward");
    without_target["layers"][2].erase("animation");
    const auto no_target = chronon3d::render_plan::decode_render_plan(without_target);
    REQUIRE(no_target);
    CHECK_FALSE(no_target->layers[2].animation);
}

TEST_CASE("shared path arc-length sampling follows cubic paths deterministically") {
    using namespace chronon3d;
    using namespace chronon3d::scene;
    constexpr float k = 0.55228475f;
    const std::vector<PathSampleCommand> circle{
        {PathSampleCommandType::MoveTo, {1, 0}},
        {PathSampleCommandType::CubicTo, {0, 1}, {1, k}, {k, 1}},
        {PathSampleCommandType::CubicTo, {-1, 0}, {-k, 1}, {-1, k}},
        {PathSampleCommandType::CubicTo, {0, -1}, {-1, -k}, {-k, -1}},
        {PathSampleCommandType::CubicTo, {1, 0}, {k, -1}, {1, -k}},
        {PathSampleCommandType::Close}
    };
    const PathArcLengthTable table(circle);
    CHECK(table.length() == doctest::Approx(6.284f).epsilon(0.002));
    const std::array<Vec2, 5> expected{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 0}}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto point = table.point_at(static_cast<float>(i) * 0.25f);
        CHECK(point.x == doctest::Approx(expected[i].x).epsilon(0.002));
        CHECK(point.y == doctest::Approx(expected[i].y).epsilon(0.002));
    }
    CHECK(table.point_at(1.25f, false).x > 1.0f);
    CHECK(table.point_at(1.25f, true).x == doctest::Approx(1.0f));
    const auto tangent = table.tangent_at(0.25f);
    CHECK(tangent.x < -0.99f);
    CHECK(tangent.y == doctest::Approx(0.0f).epsilon(0.03));
}

TEST_CASE("follow_path decoding fingerprints progress and compiles arc-length motion") {
    using nlohmann::json;
    constexpr float k = 0.55228475f;
    const json path = json::array({
        {{"type", "move_to"}, {"point", {160.0, 80.0}}},
        {{"type", "cubic_to"}, {"point", {170.0, 90.0}}, {"control1", {160.0 + 10.0 * k, 80.0}}, {"control2", {170.0, 90.0 - 10.0 * k}}},
        {{"type", "cubic_to"}, {"point", {160.0, 100.0}}, {"control1", {170.0, 90.0 + 10.0 * k}}, {"control2", {160.0 + 10.0 * k, 100.0}}},
        {{"type", "cubic_to"}, {"point", {150.0, 90.0}}, {"control1", {160.0 - 10.0 * k, 100.0}}, {"control2", {150.0, 90.0 + 10.0 * k}}},
        {{"type", "cubic_to"}, {"point", {160.0, 80.0}}, {"control1", {150.0, 90.0 - 10.0 * k}}, {"control2", {160.0 - 10.0 * k, 80.0}}},
        {{"type", "close"}}
    });
    auto make_plan = [&](float end_progress, bool clamp_end = true) {
        auto source = render_plan_test::base_plan(json::array({{
            {"id", "follower"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
            {"follow_path", {
                {"path_commands", path}, {"auto_orient", true}, {"clamp_end", clamp_end},
                {"offset_deg", 0.0},
                {"progress", {{"easing", "linear"}, {"keyframes", json::array({
                    {{"frame", 0}, {"value", 0.0}}, {{"frame", 40}, {"value", end_progress}}
                })}}}
            }}
        }}));
        source["canvas"]["duration_frames"] = 40;
        return source;
    };
    const auto decoded = chronon3d::render_plan::decode_render_plan(make_plan(1.0f));
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].follow_path);
    REQUIRE(decoded->layers[0].follow_path->path_commands.size() == 6);
    const auto changed = chronon3d::render_plan::decode_render_plan(make_plan(1.25f));
    REQUIRE(changed);
    CHECK(decoded->content_fingerprint != changed->content_fingerprint);

    const auto evaluators = chronon3d::render_plan::detail::make_follow_path_evaluators(
        *decoded->layers[0].follow_path, {0, 0, 0}, {0, 0, 0}, 320, 180,
        chronon3d::FrameRate{30, 1});
    REQUIRE(evaluators.position);
    REQUIRE(evaluators.rotation);
    const std::array<chronon3d::Vec2, 5> expected{{{0, -10}, {10, 0}, {0, 10}, {-10, 0}, {0, -10}}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto point = evaluators.position(static_cast<double>(i) * 10.0);
        CHECK(point.x == doctest::Approx(expected[i].x).epsilon(0.01));
        CHECK(point.y == doctest::Approx(expected[i].y).epsilon(0.01));
    }
    CHECK(evaluators.rotation(10.0).z == doctest::Approx(90.0f).epsilon(0.5));

    const auto unclamped_plan = chronon3d::render_plan::decode_render_plan(make_plan(1.25f, false));
    REQUIRE(unclamped_plan);
    const auto unclamped = chronon3d::render_plan::detail::make_follow_path_evaluators(
        *unclamped_plan->layers[0].follow_path, {0, 0, 0}, {0, 0, 0}, 320, 180,
        chronon3d::FrameRate{30, 1});
    const auto clamped_endpoint = evaluators.position(40.0);
    const auto extrapolated_endpoint = unclamped.position(40.0);
    CHECK(glm::length(glm::vec2{extrapolated_endpoint.x - clamped_endpoint.x,
                                extrapolated_endpoint.y - clamped_endpoint.y}) > 2.0f);

    auto bad = make_plan(1.0f);
    bad["layers"][0]["animation"] = {{"tracks", json::array({{
        {"property", "position_x"}, {"keyframes", {{{"frame", 0}, {"value", 0.0}}}}
    }})}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad));
}

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

TEST_CASE("render plan v3 accepts resolver-backed Lottie shape assets") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":320,"height":180,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"lottie","type":"shape","size":[100,100],
        "shape":{"type":"path","lottie":"shapes/mark.json"}}],
      "output":{"path":"out.png"}
    })JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const auto decode_message = decoded.has_value() ? std::string{} :
        decoded.error().path + ": " + decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_message);
    REQUIRE(decoded->layers[0].shape.has_value());
    CHECK(decoded->layers[0].shape->lottie_asset == "shapes/mark.json");
    CHECK_FALSE(decoded->layers[0].shape->svg_asset.has_value());
    auto changed = source;
    changed["layers"][0]["shape"]["lottie"] = "shapes/other.json";
    const auto changed_decoded = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_decoded.has_value());
    CHECK(decoded->content_fingerprint != changed_decoded->content_fingerprint);
}

TEST_CASE("render plan v3 decodes audio mix, fingerprints it, and warns on clipped tail") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":320,"height":180,"fps_num":30,"fps_den":1,"duration_frames":12},
      "audio":{"master_gain_db":-2,"duck_amount_db":-8,"duck_attack_ms":100,
        "duck_release_ms":350,"clips":[{"id":"bed","asset":"audio/music.wav",
          "clip_in":0,"clip_out":20,"start":4,"gain_db":-3,"role":"music",
          "gain":{"property":"gain_db","keyframes":[
            {"frame":0,"value":-3},{"frame":10,"value":-6}]}}]},
      "layers":[],"output":{"path":"out.mp4"}
    })JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->audio.has_value());
    REQUIRE(decoded->audio->clips.size() == 1);
    CHECK(decoded->audio->clips[0].asset == "audio/music.wav");
    CHECK(decoded->audio->clips[0].clip_out->integral() == 20);
    CHECK(decoded->audio->clips[0].gain->property == "gain_db");
    CHECK(std::find(decoded->warnings.begin(), decoded->warnings.end(),
                    "audio_clip_tail_clipped:bed") != decoded->warnings.end());
    auto changed = source;
    changed["audio"]["clips"][0]["gain_db"] = -4;
    const auto changed_decoded = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_decoded.has_value());
    CHECK(decoded->content_fingerprint != changed_decoded->content_fingerprint);
    source["audio"]["clips"][0]["gain_db"] = "loud";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan text spans preserve byte ranges and affect deterministic fingerprint") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"caption","type":"text","text":"Hi future",
        "style":{"font":"fonts/roboto.ttf","font_size":32,"fill":"#FFFFFF"},
        "spans":[{"start":3,"end":9,"semantic_id":"word-0",
          "style":{"color":"#FFB020","font_size_mult":1.3,
            "stroke":{"color":"#FF0000","width_em":0.08},
            "highlight":{"background":"#000000","padding_em":0.5,"radius_em":0.2}}}]}],
      "output":{"path":"out.png"}})JSON");
    const auto first = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = first.has_value() ? std::string{} :
        first.error().path + ": " + first.error().message;
    REQUIRE_MESSAGE(first.has_value(), decode_error);
    REQUIRE(first->layers[0].text_spans.size() == 1);
    CHECK(first->layers[0].text_spans[0].byte_start == 3);
    CHECK(first->layers[0].text_spans[0].byte_end == 9);
    CHECK(first->layers[0].text_spans[0].semantic_id == "word-0");
    REQUIRE(first->layers[0].text_spans[0].stroke.has_value());
    CHECK(first->layers[0].text_spans[0].stroke->width_em == doctest::Approx(0.08f));
    REQUIRE(first->layers[0].text_spans[0].highlight.has_value());
    CHECK(first->layers[0].text_spans[0].highlight->background.has_value());
    CHECK(first->layers[0].text_spans[0].highlight->padding_em == doctest::Approx(0.5f));
    CHECK(first->layers[0].text_spans[0].highlight->radius_em == doctest::Approx(0.2f));
    CHECK(first->content_fingerprint ==
          chronon3d::render_plan::compute_render_plan_content_fingerprint(*first));
    const auto repeat = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeat.has_value());
    CHECK(first->content_fingerprint == repeat->content_fingerprint);
    source["layers"][0]["spans"][0]["style"]["color"] = "#00FF00";
    const auto recolored = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(recolored.has_value());
    CHECK(first->content_fingerprint != recolored->content_fingerprint);
}

TEST_CASE("render plan V3 decodes and fingerprints deterministic Wiggly selectors") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":320,"height":180,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"title","type":"text","text":"Wiggle",
        "style":{"font":"fonts/roboto.ttf","font_size":24,"fill":"#FFFFFF"},
        "text_animators":[{"id":"w","selectors":[{"shape":"wiggly",
          "wiggly_frequency":0.75,"wiggly_amplitude":0.4,"wiggly_seed":1234}],
          "properties":[{"property":"position_y","keyframes":[{"frame":0,"value":4}]}]}]}],
      "output":{"path":"out.png"}})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    const auto& selector = decoded->layers[0].text_animators[0].selectors[0];
    CHECK(selector.shape == "wiggly");
    CHECK(selector.wiggly_frequency == doctest::Approx(0.75f));
    CHECK(selector.wiggly_amplitude == doctest::Approx(0.4f));
    CHECK(selector.wiggly_seed == 1234u);
    const auto original_fingerprint = decoded->content_fingerprint;
    source["layers"][0]["text_animators"][0]["selectors"][0]["wiggly_seed"] = 5678;
    const auto changed = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed.has_value());
    CHECK(changed->content_fingerprint != original_fingerprint);

    source["layers"][0]["text_animators"][0]["selectors"][0]["wiggly_amplitude"] = 1.1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan V3 decodes seeded noise selectors and index step") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":320,"height":180,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"title","type":"text","text":"Index",
        "style":{"font":"fonts/roboto.ttf","font_size":24,"fill":"#FFFFFF"},
        "text_animators":[{"id":"n","selectors":[
          {"shape":"noise","noise_frequency":0.8,"noise_amplitude":0.7,"noise_seed":99},
          {"order":"index","index_step":3}],
          "properties":[{"property":"position_y","keyframes":[{"frame":0,"value":4}]}]}]}],
      "output":{"path":"out.png"}})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    const auto& selectors = decoded->layers[0].text_animators[0].selectors;
    REQUIRE(selectors.size() == 2);
    CHECK(selectors[0].shape == "noise");
    CHECK(selectors[0].noise_frequency == doctest::Approx(0.8f));
    CHECK(selectors[0].noise_seed == 99u);
    CHECK(selectors[1].index_step == 3u);
    const auto fingerprint = decoded->content_fingerprint;
    source["layers"][0]["text_animators"][0]["selectors"][1]["index_step"] = 4;
    const auto changed = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != fingerprint);
    source["layers"][0]["text_animators"][0]["selectors"][1]["index_step"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
}

TEST_CASE("render plan rejects overlapping text span byte ranges") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},
      "layers":[{"id":"caption","type":"text","text":"Hello",
        "style":{"font":"font.ttf","font_size":20,"fill":"#FFFFFF"},
        "spans":[{"start":0,"end":4,"style":{"color":"#FF0000"}},
                  {"start":3,"end":5,"style":{"color":"#00FF00"}}]}],
      "output":{"path":"out.png"}})JSON");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan decodes pre-layout text fit modes") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"caption","type":"text","text":"A responsive caption",
        "size":[240,48],
        "style":{"font":"font.ttf","font_size":28,"fill":"#FFFFFF",
          "fit_mode":"grow_and_shrink","min_font_size":14,"max_font_size":36}}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = plan.has_value() ? std::string{} :
        plan.error().path + ": " + plan.error().message;
    REQUIRE_MESSAGE(plan.has_value(), decode_error);
    REQUIRE(plan->layers[0].style.has_value());
    CHECK(plan->layers[0].style->text_fit_mode == "grow_and_shrink");
    CHECK(plan->layers[0].style->min_font_size == doctest::Approx(14.0f));
    CHECK(plan->layers[0].style->max_font_size == doctest::Approx(36.0f));
    const auto repeat = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeat.has_value());
    CHECK(plan->content_fingerprint == repeat->content_fingerprint);
    source["layers"][0]["style"]["fit_mode"] = "shrink_only";
    const auto shrink = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(shrink.has_value());
    CHECK(plan->content_fingerprint != shrink->content_fingerprint);
    source["layers"][0]["style"]["min_font_size"] = 40;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}
