#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/scene/path_sampling.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>

#include "test_render_plan_shape_helpers.hpp"
#include "../../src/render_plan/render_plan_compiler_detail.hpp"

#include <utility>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <cmath>
#include <string>
#include <nlohmann/json.hpp>

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

TEST_CASE("V3 camera shake is bounded, seeded, and part of plan identity") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "camera-layer"}, {"type", "color"}, {"color", {0, 0, 0, 1}}}
    }));
    source["camera"] = {{"type", "perspective"}};
    source["camera_shake"] = {{"amplitude_px", 12.0}, {"frequency_hz", 6.0},
        {"decay", 0.4}, {"seed", 19}, {"rotation_amplitude_deg", 1.5},
        {"start_frame", 3}, {"duration_frames", 20}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->camera_shake);
    CHECK(decoded->camera_shake->seed == 19);
    auto changed = source;
    changed["camera_shake"]["seed"] = 20;
    const auto other = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(other);
    CHECK(decoded->content_fingerprint != other->content_fingerprint);
    changed["camera_shake"]["frequency_hz"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed));
    source.erase("camera");
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

TEST_CASE("render plan decodes named semantic span selector for word animation") {
    const auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"caption","type":"text","text":"Hi future",
        "style":{"font":"fonts/roboto.ttf","font_size":32,"fill":"#FFFFFF"},
        "spans":[{"start":3,"end":9,"semantic_id":"word-0","style":{"color":"#FFB020"}}],
        "text_animators":[{"id":"word-highlight",
          "selectors":[{"unit":"glyph","semantic_id":"word-0","shape":"square",
            "amount":{"keyframes":[{"frame":0,"value":0},{"frame":8,"value":100}]}}],
          "properties":[
            {"property":"opacity","keyframes":[
              {"frame":0,"value":0.0},{"frame":8,"value":1.0}]},
            {"property":"fill_color","keyframes":[
              {"frame":0,"value":[1.0,0.7,0.0,1.0]}]}]}]}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    std::string decode_error;
    if (!plan.has_value()) decode_error = plan.error().message;
    CAPTURE(decode_error);
    REQUIRE(plan.has_value());
    REQUIRE(plan->layers[0].text_animators.size() == 1);
    REQUIRE(plan->layers[0].text_animators[0].selectors.size() == 1);
    CHECK(plan->layers[0].text_animators[0].selectors[0].semantic_id == "word-0");
    REQUIRE(plan->layers[0].text_animators[0].properties.size() == 2);
    CHECK(plan->layers[0].text_animators[0].properties[1].property == "fill_color");
    REQUIRE(plan->layers[0].text_animators[0].properties[1].keyframes[0].value.size() == 4);
    CHECK(plan->layers[0].text_animators[0].properties[1].keyframes[0].value[1] == doctest::Approx(0.7f));
    auto renamed = source;
    renamed["layers"][0]["spans"][0]["semantic_id"] = "word-1";
    renamed["layers"][0]["text_animators"][0]["selectors"][0]["semantic_id"] = "word-1";
    const auto renamed_plan = chronon3d::render_plan::decode_render_plan(renamed);
    REQUIRE(renamed_plan.has_value());
    CHECK(plan->content_fingerprint != renamed_plan->content_fingerprint);
}

TEST_CASE("render plan decodes shutter-angle motion blur settings") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":24,"fps_den":1,"duration_frames":25},
      "layers":[{"id":"moving","type":"shape","size":[20,20],
        "shape":{"type":"rect"},"motion_blur":{"shutter_angle":180,"samples":12},
        "animation":{"tracks":[{"property":"position","keyframes":[
          {"frame":0,"value":[0,0]},{"frame":24,"value":[480,0]}]}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    if (!plan) MESSAGE("motion blur plan decode failed: " << plan.error().message);
    REQUIRE(plan.has_value());
    REQUIRE(plan->layers[0].motion_blur.has_value());
    CHECK(plan->layers[0].motion_blur->shutter_angle == doctest::Approx(180.0f));
    CHECK(plan->layers[0].motion_blur->samples == 12);
    const auto blur = chronon3d::render_plan::detail::motion_blur_params_at(
        plan->layers[0], 12.0, chronon3d::FrameRate{24, 1});
    REQUIRE(blur.has_value());
    CHECK(blur->length == doctest::Approx(10.0f));
    CHECK(blur->angle == doctest::Approx(0.0f));
    CHECK(blur->samples == 12);
    const auto repeated = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeated.has_value());
    CHECK(plan->content_fingerprint == repeated->content_fingerprint);
    source["layers"][0]["motion_blur"]["shutter_angle"] = 90;
    const auto changed_shutter = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed_shutter.has_value());
    CHECK(plan->content_fingerprint != changed_shutter->content_fingerprint);
    source["layers"][0]["motion_blur"]["shutter_angle"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());

    auto diagonal = source;
    diagonal["layers"][0]["motion_blur"]["shutter_angle"] = 180;
    diagonal["layers"][0]["animation"]["tracks"] = nlohmann::json::parse(R"JSON([
      {"property":"position_x","keyframes":[{"frame":0,"value":0},{"frame":24,"value":480}]},
      {"property":"position_y","keyframes":[{"frame":0,"value":0},{"frame":24,"value":240}]}
    ])JSON");
    const auto diagonal_plan = chronon3d::render_plan::decode_render_plan(diagonal);
    REQUIRE(diagonal_plan.has_value());
    const auto diagonal_blur = chronon3d::render_plan::detail::motion_blur_params_at(
        diagonal_plan->layers[0], 12.0, chronon3d::FrameRate{24, 1});
    REQUIRE(diagonal_blur.has_value());
    CHECK(diagonal_blur->length == doctest::Approx(std::sqrt(125.0f)));
    CHECK(diagonal_blur->angle == doctest::Approx(std::atan2(5.0f, 10.0f) * 180.0f / 3.14159265358979323846f));
}

TEST_CASE("render plan animation loop modes decode and affect plan identity") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":32,"height":32,"fps_num":30,"fps_den":1,"duration_frames":60},
      "layers":[{"id":"loop","type":"shape","size":[8,8],"shape":{"type":"rect"},
        "animation":{"tracks":[{"property":"position_x","loop_mode":"cycle","keyframes":[
          {"frame":0,"value":0},{"frame":10,"value":20}]}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto cycle = chronon3d::render_plan::decode_render_plan(source);
    if (!cycle) MESSAGE("loop plan decode failed: " << cycle.error().message);
    REQUIRE(cycle.has_value());
    CHECK(cycle->layers[0].animation->tracks[0].loop_mode == "cycle");

    source["layers"][0]["animation"]["tracks"][0]["loop_mode"] = "pingpong";
    const auto pingpong = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(pingpong.has_value());
    CHECK(pingpong->content_fingerprint != cycle->content_fingerprint);

    source["layers"][0]["animation"]["tracks"][0]["loop_mode"] = "offset";
    const auto offset = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(offset.has_value());
    CHECK(offset->layers[0].animation->tracks[0].loop_mode == "offset");

    source["layers"][0]["animation"]["tracks"][0]["loop_mode"] = "repeat";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());

    source["layers"][0]["animation"]["tracks"][0]["property"] = "fill_color";
    source["layers"][0]["animation"]["tracks"][0]["loop_mode"] = "offset";
    source["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["value"] = {1, 1, 1, 1};
    source["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["value"] = {0, 0, 0, 1};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan decodes bounded wiggle drivers and fingerprints their seed") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":32,"height":32,"fps_num":30,"fps_den":1,"duration_frames":60},
      "layers":[{"id":"wiggle","type":"shape","size":[8,8],"shape":{"type":"rect"},
        "animation":{"tracks":[{"property":"opacity","wiggle":{"frequency":2,"amplitude":0.2,"seed":42},
          "keyframes":[{"frame":0,"value":0.5},{"frame":30,"value":0.5}]}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    const auto& track = decoded->layers[0].animation->tracks[0];
    REQUIRE(track.wiggle.has_value());
    CHECK(track.wiggle->frequency == doctest::Approx(2.0f));
    CHECK(track.wiggle->amplitude == doctest::Approx(0.2f));
    CHECK(track.wiggle->seed == 42);

    source["layers"][0]["animation"]["tracks"][0]["wiggle"]["seed"] = 43;
    const auto changed_seed = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed_seed.has_value());
    CHECK(changed_seed->content_fingerprint != decoded->content_fingerprint);
    source["layers"][0]["animation"]["tracks"][0]["wiggle"]["frequency"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
    source["layers"][0]["animation"]["tracks"][0]["wiggle"]["frequency"] = 2;
    source["layers"][0]["animation"]["tracks"][0]["property"] = "position";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan auto_orient requires position motion and excludes rotation tracks") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":32,"height":32,"fps_num":30,"fps_den":1,"duration_frames":20},
      "layers":[{"id":"oriented","type":"shape","size":[8,8],"shape":{"type":"rect"},
        "auto_orient":true,
        "animation":{"tracks":[{"property":"position","keyframes":[
          {"frame":0,"value":[0,0]},{"frame":10,"value":[10,10]}]}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    CHECK(decoded->layers[0].auto_orient);
    source["layers"][0]["auto_orient"] = false;
    const auto fixed_rotation = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(fixed_rotation.has_value());
    CHECK(fixed_rotation->content_fingerprint != decoded->content_fingerprint);
    source["layers"][0]["auto_orient"] = true;

    source["layers"][0]["animation"]["tracks"].push_back({
        {"property", "rotation_z"}, {"keyframes", {{{"frame", 0}, {"value", 0}}, {{"frame", 10}, {"value", 1}}}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
    source["layers"][0]["animation"]["tracks"].erase(1);
    source["layers"][0]["animation"]["tracks"][0]["keyframes"].erase(1);
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("v3 keyframes decode outgoing timing and report ignored final-key overrides") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":320,"height":180,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"animated","type":"shape","size":[10,10],"shape":{"type":"rect"},
        "animation":{"tracks":[{"property":"opacity","easing":"linear","keyframes":[
          {"frame":0,"value":0,"interpolation":"hold"},
          {"frame":10,"value":1,"easing":"bezier","bezier_curve":[0.2,0.1,0.8,0.9]}]}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    if (!plan) MESSAGE("outgoing timing plan decode failed: " << plan.error().message);
    if (!plan) INFO(plan.error().message);
    REQUIRE(plan);
    const auto& keys = plan->layers[0].animation->tracks[0].keyframes;
    REQUIRE(keys.size() == 2);
    CHECK(keys[0].interpolation == "hold");
    CHECK(keys[1].easing == "bezier");
    CHECK(std::count(plan->warnings.begin(), plan->warnings.end(),
                     "animation_last_key_timing_ignored") == 1);

    source["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["bezier_curve"] = {0.2, 0, 1.1, 1};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
    source["layers"][0]["animation"]["tracks"][0]["keyframes"][0].erase("interpolation");
    source["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["bezier_curve"] =
        {0.2, std::numeric_limits<float>::quiet_NaN(), 0.8, 0.9};
    source["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["easing"] = "bezier";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));

    source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":320,"height":180,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"animated","type":"shape","size":[10,10],"shape":{"type":"rect"},
        "animation":{"tracks":[{"property":"opacity","keyframes":[
          {"frame":0,"value":0,"interpolation":"auto_bezier"},
          {"frame":5,"value":1,"interpolation":"linear","roving":true},
          {"frame":10,"value":0}]}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto timing_plan = chronon3d::render_plan::decode_render_plan(source);
    if (!timing_plan) INFO(timing_plan.error().message);
    REQUIRE(timing_plan);
    {
        const auto& timing_keys = timing_plan->layers[0].animation->tracks[0].keyframes;
        CHECK(timing_keys[0].interpolation == "auto_bezier");
        CHECK(timing_keys[1].roving);
    }

    source["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["interpolation"] = "bezier";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
    source["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["interpolation"] = "linear";
    source["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["temporal_in_dx"] = -2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
}

TEST_CASE("per-key temporal handles, roving and AutoBezier preserve AnimatedValue semantics") {
    using namespace chronon3d;

    AnimatedValue<float> custom{0.0f};
    chronon3d::render_plan::AnimationKeyframePlan a;
    a.frame = Frame{0};
    a.value = {0.0f};
    a.interpolation = "bezier";
    a.temporal_out_dx = 3.0f;
    a.temporal_out_dy = 0.0f;
    chronon3d::render_plan::AnimationKeyframePlan b;
    b.frame = Frame{10};
    b.value = {10.0f};
    b.interpolation = "bezier";
    b.temporal_in_dx = -3.0f;
    b.temporal_in_dy = -5.0f;
    custom.add_keyframe(a.frame, 0.0f, InterpMode::Bezier,
        0.0f, 0.0f, *a.temporal_out_dx, *a.temporal_out_dy);
    custom.add_keyframe(b.frame, 10.0f, InterpMode::Bezier,
        *b.temporal_in_dx, *b.temporal_in_dy, 0.0f, 0.0f);
    CHECK(custom.evaluate(5.0) != doctest::Approx(5.0f));

    AnimatedValue<float> auto_curve{0.0f};
    auto_curve.add_keyframe(Frame{0}, 0.0f, InterpMode::AutoBezier, 0, 0, 0, 0);
    auto_curve.add_keyframe(Frame{10}, 2.0f, InterpMode::AutoBezier, 0, 0, 0, 0);
    auto_curve.add_keyframe(Frame{20}, 10.0f, InterpMode::AutoBezier, 0, 0, 0, 0);
    auto_curve.compute_auto_beziers();
    CHECK(auto_curve.evaluate(5.0) > 0.0f);
    CHECK(auto_curve.evaluate(5.0) == auto_curve.evaluate(5.0));

    AnimatedValue<float> roving{0.0f};
    roving.add_keyframe(Frame{0}, 0.0f);
    roving.add_keyframe(Frame{2}, 2.0f, EasingCurve{}, true);
    roving.add_keyframe(Frame{8}, 8.0f, EasingCurve{}, true);
    roving.add_keyframe(Frame{12}, 12.0f);
    roving.compute_roving();
    CHECK(roving.keyframes()[1].frame == Frame{2});
    CHECK(roving.keyframes()[2].frame == Frame{8});
    roving.add_keyframe(Frame{4}, 3.0f, EasingCurve{}, true);
    roving.compute_roving();
    CHECK(roving.keyframes()[1].frame == Frame{2});
    CHECK(roving.keyframes()[2].frame == Frame{3});
    CHECK(roving.keyframes()[3].frame == Frame{8});
}

TEST_CASE("shared keyframe easing resolves hold and deterministic cubic curves") {
    chronon3d::render_plan::AnimationTrackPlan track;
    track.easing = "in_quad";
    chronon3d::render_plan::AnimationKeyframePlan key;
    key.interpolation = "hold";
    CHECK(chronon3d::render_plan::detail::animation_keyframe_easing(track, key) ==
          chronon3d::Easing::Hold);
    key.interpolation.reset();
    CHECK(chronon3d::render_plan::detail::animation_keyframe_easing(track, key) ==
          chronon3d::Easing::InQuad);
    key.easing = "bezier";
    key.bezier_curve = std::array<float, 4>{0.2f, 0.1f, 0.8f, 0.9f};
    const auto first = chronon3d::render_plan::detail::animation_keyframe_easing(track, key).apply(0.37f);
    const auto second = chronon3d::render_plan::detail::animation_keyframe_easing(track, key).apply(0.37f);
    CHECK(first == second);
    CHECK(first == doctest::Approx(0.3544011f).epsilon(1e-6));
    CHECK(chronon3d::render_plan::detail::animation_keyframe_easing(track, key).apply(0.0f) == 0.0f);
    CHECK(chronon3d::render_plan::detail::animation_keyframe_easing(track, key).apply(1.0f) == 1.0f);
    key.easing.reset();
    key.bezier_curve.reset();
    key.interpolation = "hold";
    chronon3d::AnimatedValue<float> sampled{5.0f};
    sampled.add_keyframe(chronon3d::Frame{0}, 10.0f,
                         chronon3d::render_plan::detail::animation_keyframe_easing(track, key));
    sampled.add_keyframe(chronon3d::Frame{10}, 20.0f, chronon3d::EasingCurve{});
    CHECK(sampled.evaluate(0.0) == 10.0f);
    CHECK(sampled.evaluate(5.0) == 10.0f);
    CHECK(sampled.evaluate(10.0) == 20.0f);
}

TEST_CASE("camera consumer uses outgoing per-key interpolation") {
    auto source = render_plan_test::base_plan();
    source["camera"] = {{"type", "perspective"}, {"position", {0, 0, -1000}},
        {"rotation_deg", {0, 0, 0}}, {"fov_deg", 50}, {"near", 1},
        {"far", 10000}, {"zoom", 1}};
    source["camera_animation"] = nlohmann::json::parse(R"JSON({"tracks":[{
      "property":"camera_position_z","keyframes":[
        {"frame":0,"value":-600,"interpolation":"hold"},
        {"frame":10,"value":-1000}]}]})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(plan);
    const auto descriptor = chronon3d::render_plan::detail::make_camera_descriptor(*plan);
    REQUIRE(descriptor);
    const auto* tracks = std::get_if<chronon3d::camera_v1::PoseTracksSource>(&descriptor->source);
    REQUIRE(tracks);
    CHECK(tracks->position.evaluate(5.0).z == doctest::Approx(-600.0f));
    CHECK(tracks->position.evaluate(10.0).z == doctest::Approx(-1000.0f));
}

TEST_CASE("camera render-plan scalar axes remain independent at shared keyframes") {
    auto source = render_plan_test::base_plan();
    source["camera"] = {{"type", "perspective"}, {"position", {0, 0, -1000}},
        {"rotation_deg", {0, 0, 0}}, {"fov_deg", 50}, {"near", 1},
        {"far", 10000}, {"zoom", 1}};
    source["camera_animation"] = nlohmann::json::parse(R"JSON({"tracks":[
      {"property":"camera_position_x","keyframes":[{"frame":0,"value":10},{"frame":2,"value":20}]},
      {"property":"camera_position_y","keyframes":[{"frame":0,"value":30},{"frame":2,"value":40}]},
      {"property":"camera_position_z","keyframes":[{"frame":0,"value":-500},{"frame":2,"value":-800}]},
      {"property":"camera_rotation_x","keyframes":[{"frame":0,"value":1},{"frame":2,"value":3}]},
      {"property":"camera_rotation_y","keyframes":[{"frame":0,"value":4},{"frame":2,"value":6}]},
      {"property":"camera_rotation_z","keyframes":[{"frame":0,"value":7},{"frame":2,"value":9}]}
    ]})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(plan);
    const auto descriptor = chronon3d::render_plan::detail::make_camera_descriptor(*plan);
    REQUIRE(descriptor);
    const auto* tracks = std::get_if<chronon3d::camera_v1::PoseTracksSource>(&descriptor->source);
    REQUIRE(tracks);
    for (const auto& component : tracks->position_components) REQUIRE(component.has_value());
    for (const auto& component : tracks->rotation_components) REQUIRE(component.has_value());
    CHECK(tracks->position_components[0]->evaluate(0.0) == doctest::Approx(10.f));
    CHECK(tracks->position_components[1]->evaluate(0.0) == doctest::Approx(30.f));
    CHECK(tracks->position_components[2]->evaluate(0.0) == doctest::Approx(-500.f));
    CHECK(tracks->position_components[0]->evaluate(1.0) == doctest::Approx(15.f));
    CHECK(tracks->position_components[1]->evaluate(1.0) == doctest::Approx(35.f));
    CHECK(tracks->position_components[2]->evaluate(1.0) == doctest::Approx(-650.f));
    CHECK(tracks->position_components[0]->evaluate(2.0) == doctest::Approx(20.f));
    CHECK(tracks->position_components[1]->evaluate(2.0) == doctest::Approx(40.f));
    CHECK(tracks->position_components[2]->evaluate(2.0) == doctest::Approx(-800.f));
    CHECK(tracks->rotation_components[0]->evaluate(2.0) == doctest::Approx(3.f));
    CHECK(tracks->rotation_components[1]->evaluate(2.0) == doctest::Approx(6.f));
    CHECK(tracks->rotation_components[2]->evaluate(2.0) == doctest::Approx(9.f));
}

TEST_CASE("render plan exposes true temporal shutter accumulation settings") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":24,"fps_den":1,"duration_frames":25},
      "temporal_motion_blur":{"shutter_angle":180,"samples":12},
      "layers":[{"id":"moving","type":"shape","size":[20,20],
        "shape":{"type":"rect"},"animation":{"tracks":[{"property":"rotation",
          "keyframes":[{"frame":0,"value":[0,0,0]},{"frame":24,"value":[0,0,360]}]}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(plan.has_value());
    REQUIRE(plan->temporal_motion_blur.has_value());
    CHECK(plan->temporal_motion_blur->shutter_angle == doctest::Approx(180.0f));
    CHECK(plan->temporal_motion_blur->samples == 12);
    const auto repeated = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeated.has_value());
    CHECK(plan->content_fingerprint == repeated->content_fingerprint);
    source["temporal_motion_blur"]["samples"] = 16;
    const auto changed = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed.has_value());
    CHECK(plan->content_fingerprint != changed->content_fingerprint);
    source["temporal_motion_blur"]["samples"] = 1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
    source["temporal_motion_blur"]["samples"] = 12;
    source["layers"][0]["motion_blur"] = {{"shutter_angle", 180}, {"samples", 12}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan safe insets enable responsive safe-area clamping and affect identity") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":1080,"height":1920,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"caption","type":"text","text":"Hello","font":"tests/fixtures/Inter-Bold.ttf",
        "position":[540,1700],"safe_insets":{"top":80,"right":60,"bottom":220,"left":60},
        "style":{"font":"tests/fixtures/Inter-Bold.ttf","font_size":64,"fill":"#FFFFFF"}}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    if (!plan) FAIL(plan.error().message);
    REQUIRE(plan.has_value());
    REQUIRE(plan->layers[0].safe_insets.has_value());
    CHECK((*plan->layers[0].safe_insets)[0] == doctest::Approx(80.0f));
    CHECK((*plan->layers[0].safe_insets)[1] == doctest::Approx(60.0f));
    CHECK((*plan->layers[0].safe_insets)[2] == doctest::Approx(220.0f));
    CHECK((*plan->layers[0].safe_insets)[3] == doctest::Approx(60.0f));
    source["layers"][0]["safe_insets"]["bottom"] = 180;
    const auto adjusted = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(adjusted.has_value());
    CHECK(plan->content_fingerprint != adjusted->content_fingerprint);
    source["layers"][0]["safe_insets"]["bottom"] = -1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
    source["layers"][0]["safe_insets"] = {
        {"top", 1000}, {"bottom", 1000}, {"left", 60}, {"right", 60}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan decodes and fingerprints deterministic particle emitters") {
    const auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":24,"fps_den":1,"duration_frames":48},
      "layers":[{"id":"dust","type":"particles","particle_emitter":{
        "count":200,"seed":4728,"emitter_size":[640,360],
        "velocity_min":[-8,-20],"velocity_max":[8,-4],
        "lifetime":[3,8],"size":[2,14],"gravity":1.5,"turbulence":0.35,
        "blend_mode":"add",
        "color_ramp":[{"position":0,"color":[0.6,0.5,0.4,0]},
                       {"position":1,"color":[0.8,0.7,0.6,1]}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error=plan.has_value()?std::string{}:plan.error().message;
    REQUIRE_MESSAGE(plan.has_value(),decode_error);
    REQUIRE(plan->layers.size() == 1);
    CHECK(plan->layers[0].type == chronon3d::render_plan::LayerType::Particles);
    REQUIRE(plan->layers[0].particle_emitter.has_value());
    CHECK(plan->layers[0].particle_emitter->count == 200);
    CHECK(plan->layers[0].particle_emitter->seed == 4728);
    CHECK(plan->layers[0].particle_emitter->blend_mode == chronon3d::BlendMode::Add);
    REQUIRE(plan->layers[0].particle_emitter->color_ramp.color_stops.size() == 2);

    auto changed = source;
    changed["layers"][0]["particle_emitter"]["seed"] = 4729;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan.has_value());
    CHECK(plan->content_fingerprint != changed_plan->content_fingerprint);

    changed = source;
    changed["layers"][0]["particle_emitter"]["blend_mode"] = "screen";
    const auto changed_blend = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_blend.has_value());
    CHECK(changed_blend->content_fingerprint != plan->content_fingerprint);
    changed["layers"][0]["particle_emitter"]["blend_mode"] = "unsupported";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed).has_value());

    changed = source;
    changed["layers"][0]["particle_emitter"]["velocity_max"][0] = -9;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed).has_value());
}

TEST_CASE("render plan decodes screen-space radial light layers") {
    const auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":24,"fps_den":1,"duration_frames":24},
      "layers":[{"id":"warm-light","type":"light","position":[320,180],
        "light":{"radius":240,"color":"#FF8040","intensity":1.5}}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(plan.has_value());
    REQUIRE(plan->layers.size() == 1);
    REQUIRE(plan->layers[0].light.has_value());
    CHECK(plan->layers[0].light->radius == doctest::Approx(240.0f));
    CHECK(plan->layers[0].light->intensity == doctest::Approx(1.5f));
    CHECK(plan->layers[0].blend_mode == chronon3d::BlendMode::Screen);
    const auto repeated = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeated.has_value());
    CHECK(plan->content_fingerprint == repeated->content_fingerprint);

    auto changed = source;
    changed["layers"][0]["light"]["intensity"] = 1.0;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan.has_value());
    CHECK(plan->content_fingerprint != changed_plan->content_fingerprint);
    changed["layers"][0]["light"]["radius"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed).has_value());
}

TEST_CASE("render plan decodes point, spot and directional light kinds and fingerprints each field") {
    const auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":24,"fps_den":1,"duration_frames":24},
      "layers":[{"id":"key","type":"light","position":[320,180,-200],"enable_3d":true,
        "light":{"kind":"spot","radius":240,"color":"#FFE0C0","intensity":2.0,
          "direction":[0,0,1],"range":900,"decay":2,"angle":0.6,"penumbra":0.25,
          "cast_shadow":true,"shadow_radius":6,"shadow_bias":0.02}}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(plan.has_value());
    REQUIRE(plan->layers[0].light.has_value());
    const auto& light = *plan->layers[0].light;
    CHECK(light.kind == chronon3d::render_plan::LightKindPlan::Spot);
    CHECK(light.direction[2] == doctest::Approx(1.0f));
    CHECK(light.range == doctest::Approx(900.0f));
    CHECK(light.decay == doctest::Approx(2.0f));
    CHECK(light.angle == doctest::Approx(0.6f));
    CHECK(light.penumbra == doctest::Approx(0.25f));
    CHECK(light.cast_shadow);
    CHECK(light.shadow_radius == doctest::Approx(6.0f));
    CHECK(light.shadow_bias == doctest::Approx(0.02f));

    // Every new field is part of the content fingerprint, so a cache entry for
    // one light can never be reused for another.
    const auto base = plan->content_fingerprint;
    const std::vector<std::pair<const char*, nlohmann::json>> edits{
        {"range", 800.0}, {"decay", 1.0}, {"angle", 0.7}, {"penumbra", 0.5},
        {"cast_shadow", false}, {"shadow_radius", 2.0}, {"shadow_bias", 0.0},
        {"kind", "point"}};
    for (const auto& [field, value] : edits) {
        auto changed = source;
        changed["layers"][0]["light"][field] = value;
        const auto decoded = chronon3d::render_plan::decode_render_plan(changed);
        REQUIRE(decoded.has_value());
        CHECK_MESSAGE(decoded->content_fingerprint != base,
                      "light field '" << field << "' must change the fingerprint");
    }
    auto changed_direction = source;
    changed_direction["layers"][0]["light"]["direction"] = nlohmann::json::array({1, 0, 0});
    const auto direction_plan = chronon3d::render_plan::decode_render_plan(changed_direction);
    REQUIRE(direction_plan.has_value());
    CHECK(direction_plan->content_fingerprint != base);
}

TEST_CASE("render plan rejects invalid light kind parameters") {
    const auto base = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":24,"fps_den":1,"duration_frames":8},
      "layers":[{"id":"l","type":"light","position":[32,32,0],"enable_3d":true,
        "light":{"kind":"directional","radius":32,"color":"#FFFFFF","intensity":1.0,
          "direction":[0,0,-1]}}],
      "output":{"path":"out.png"}})JSON");
    REQUIRE(chronon3d::render_plan::decode_render_plan(base).has_value());

    const auto rejects = [&](auto mutate) {
        auto broken = base;
        mutate(broken["layers"][0]["light"]);
        return !chronon3d::render_plan::decode_render_plan(broken).has_value();
    };
    CHECK(rejects([](auto& light) { light["kind"] = "area"; }));
    CHECK(rejects([](auto& light) { light["direction"] = nlohmann::json::array({0, 0, 0}); }));
    CHECK(rejects([](auto& light) { light["direction"] = nlohmann::json::array({0, 0}); }));
    CHECK(rejects([](auto& light) { light["angle"] = 0.0; }));
    CHECK(rejects([](auto& light) { light["angle"] = 4.0; }));
    CHECK(rejects([](auto& light) { light["penumbra"] = 1.5; }));
    CHECK(rejects([](auto& light) { light["decay"] = -1.0; }));
    CHECK(rejects([](auto& light) { light["range"] = -5.0; }));
    CHECK(rejects([](auto& light) { light["shadow_radius"] = -1.0; }));
    CHECK(rejects([](auto& light) { light["shadow_bias"] = 99.0; }));
    CHECK(rejects([](auto& light) { light["kind"] = "radial"; light["cast_shadow"] = true; }));
}

TEST_CASE("render plan decodes deterministic noise and procedural background effects") {
    const auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":24,"fps_den":1,"duration_frames":24},
      "layers":[{"id":"background","type":"color","color":[0,0,0,1],
        "effects":[{"type":"fractal_noise","seed":19,"octaves":5,"frequency":0.025,
          "amplitude":0.8,"evolution":0.4,"contrast":1.2,"brightness":-0.1,"color_mode":"rgb"},
          {"type":"noise","amount":0.08,"size":2,"seed":42,"animated":true,
            "shadow_response":0.6,"midtone_response":1.0,"highlight_response":0.4}]}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(plan.has_value());
    REQUIRE(plan->layers[0].effects.size() == 2);
    CHECK(plan->layers[0].effects[0].kind ==
          chronon3d::render_plan::LayerPlan::EffectKindPlan::FractalNoise);
    CHECK(plan->layers[0].effects[0].seed == 19);
    CHECK(plan->layers[0].effects[0].rgb_noise);
    CHECK(plan->layers[0].effects[1].kind ==
          chronon3d::render_plan::LayerPlan::EffectKindPlan::Noise);
    CHECK(plan->layers[0].effects[1].animated);
    CHECK(plan->layers[0].effects[1].grain_shadow_response == doctest::Approx(0.6f));
    CHECK(plan->layers[0].effects[1].grain_highlight_response == doctest::Approx(0.4f));
    const auto repeated = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeated.has_value());
    CHECK(plan->content_fingerprint == repeated->content_fingerprint);
    auto changed = source;
    changed["layers"][0]["effects"][1]["highlight_response"] = 0.7;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan.has_value());
    CHECK(plan->content_fingerprint != changed_plan->content_fingerprint);
    changed["layers"][0]["effects"][0]["octaves"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed).has_value());
    changed = source;
    changed["layers"][0]["effects"][1]["shadow_response"] = 5.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed).has_value());
}

TEST_CASE("render plan expands typed cinematic background looks into fingerprinted effects") {
    const auto base = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":640,"height":360,"fps_num":24,"fps_den":1,"duration_frames":24},
      "layers":[{"id":"background","type":"shape","size":[640,360],
        "shape":{"type":"rect","fill":[0,0,0,1]},
        "background_look":{"type":"film_grade","grain":0.04,"seed":73,"vignette":0.18}}],
      "output":{"path":"out.png"}})JSON");
    const auto plan = chronon3d::render_plan::decode_render_plan(base);
    if (!plan) INFO(plan.error().message);
    REQUIRE(plan.has_value());
    REQUIRE(plan->layers[0].effects.size() == 2);
    CHECK(plan->layers[0].effects[0].kind ==
          chronon3d::render_plan::LayerPlan::EffectKindPlan::Noise);
    CHECK(plan->layers[0].effects[1].kind ==
          chronon3d::render_plan::LayerPlan::EffectKindPlan::Vignette);
    auto changed = base;
    changed["layers"][0]["background_look"]["seed"] = 74;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan.has_value());
    CHECK(plan->content_fingerprint != changed_plan->content_fingerprint);

    for (const auto& look : {
             nlohmann::json{{"type", "aurora"}},
             nlohmann::json{{"type", "liquid"}}}) {
        auto variant = base;
        variant["layers"][0]["background_look"] = look;
        const auto decoded = chronon3d::render_plan::decode_render_plan(variant);
        if (!decoded) INFO(decoded.error().message);
        REQUIRE(decoded.has_value());
        CHECK(decoded->layers[0].effects.size() == 2);
    }
}

TEST_CASE("render plan V3 decodes bounded LightRays parameters and fingerprints each control") {
    const auto rays_layer = nlohmann::json::parse(R"JSON({
      "id":"rays","type":"color","color":[0,0,0,1],
      "effects":[{"type":"light_rays","origin":[0.25,0.75],
        "length":640,"density":1.5,"color":[1,0.8,0.4,0.9],"decay":0.8}]
    })JSON");
    auto source = render_plan_test::base_plan(nlohmann::json::array({rays_layer}));

    // V2 remains closed: this V3-only effect is rejected when the producer
    // explicitly labels the same payload as V2.
    auto v2_source = source;
    v2_source["schema"] = "chronon.render-plan.v2";
    v2_source["version"] = 2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(v2_source).has_value());

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = decoded ? std::string{} : decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_error);
    REQUIRE(decoded->layers[0].effects.size() == 1);
    const auto& effect = decoded->layers[0].effects[0];
    CHECK(effect.kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::LightRays);
    CHECK(effect.light_rays_origin[0] == doctest::Approx(0.25f));
    CHECK(effect.light_rays_origin[1] == doctest::Approx(0.75f));
    CHECK(effect.light_rays_length == doctest::Approx(640.0f));
    CHECK(effect.light_rays_density == doctest::Approx(1.5f));
    CHECK(effect.light_rays_color[2] == doctest::Approx(0.4f));
    CHECK(effect.light_rays_decay == doctest::Approx(0.8f));

    const auto original_fingerprint = decoded->content_fingerprint;
    const std::vector<std::pair<std::string, nlohmann::json>> changed_values{
        {"origin", nlohmann::json::array({0.5, 0.5})},
        {"length", nlohmann::json(320.0)}, {"density", nlohmann::json(2.0)},
        {"color", nlohmann::json::array({0.5, 0.8, 0.4, 0.9})},
        {"decay", nlohmann::json(0.7)}
    };
    for (const auto& [field, value] : changed_values) {
        auto changed = source;
        changed["layers"][0]["effects"][0][field] = value;
        const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
        REQUIRE(changed_plan.has_value());
        CHECK(changed_plan->content_fingerprint != original_fingerprint);
    }

    for (const auto& invalid : std::vector<nlohmann::json>{
             {{"origin", nlohmann::json::array({-0.01, 0.5})}},
             {{"origin", nlohmann::json::array({0.5, 1.01})}},
             {{"length", 100001}}, {{"density", 8.01}},
             {{"decay", -0.01}},
             {{"color", nlohmann::json::array({1.0, 0.0, 0.0, 1.01})}}}) {
        auto malformed = source;
        for (auto it = invalid.begin(); it != invalid.end(); ++it)
            malformed["layers"][0]["effects"][0][it.key()] = it.value();
        CHECK_FALSE(chronon3d::render_plan::decode_render_plan(malformed).has_value());
    }
}

TEST_CASE("render plan V3 decodes animated solid fill and sorts keyframes") {
    const auto color_layer = nlohmann::json::parse(R"JSON({
      "id":"color","type":"color","color":[0,0,0,1],
      "animation":{"tracks":[{"property":"fill_color","keyframes":[
        {"frame":8,"value":[1,0,0,1]}, {"frame":0,"value":[0,0,1,1]}]}]}
    })JSON");
    auto source = render_plan_test::base_plan(nlohmann::json::array({color_layer}));
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = decoded ? std::string{} : decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_error);
    const auto& keys = decoded->layers[0].animation->tracks[0].keyframes;
    REQUIRE(keys.size() == 2);
    CHECK(keys[0].frame == chronon3d::Frame{0});
    CHECK(keys[1].frame == chronon3d::Frame{8});

    auto duplicate = source;
    duplicate["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["frame"] = 8;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(duplicate).has_value());
}

TEST_CASE("render plan V3 decodes typed built-in layer transitions and fingerprints parameters") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":320,"height":180,"fps_num":30,"fps_den":1,"duration_frames":30},
      "layers":[{"id":"intro","type":"color","color":[1,1,1,1],"duration_frames":30,
        "transition_in":{"id":"slide","kind":"slide","duration_frames":6,"delay_frames":3,"direction":"left","easing":"smoothstep","params":{"distance":0.75}},
        "transition_out":{"id":"flash","kind":"flash","duration_frames":8,"params":{"color":"#FF8040"}}}],
      "output":{"path":"out.png"}})JSON");
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    const std::string decode_error = decoded ? std::string{} : decoded.error().message;
    REQUIRE_MESSAGE(decoded.has_value(), decode_error);
    REQUIRE(decoded->layers[0].transition_in.has_value());
    CHECK(decoded->layers[0].transition_in->kind == "slide");
    CHECK(decoded->layers[0].transition_in->duration_frames == chronon3d::Frame{6});
    CHECK(decoded->layers[0].transition_in->delay_frames == chronon3d::Frame{3});
    CHECK(decoded->layers[0].transition_in->direction == "left");
    CHECK(decoded->layers[0].transition_in->easing == "smoothstep");
    CHECK(decoded->layers[0].transition_in->distance == doctest::Approx(0.75f));
    REQUIRE(decoded->layers[0].transition_out.has_value());
    CHECK(decoded->layers[0].transition_out->kind == "flash");
    CHECK(decoded->layers[0].transition_out->color[1] == doctest::Approx(0.2158605f));
    const auto original_fingerprint = decoded->content_fingerprint;

    source["layers"][0]["transition_in"]["params"]["distance"] = 0.5;
    const auto changed = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed.has_value());
    CHECK(changed->content_fingerprint != original_fingerprint);

    source["layers"][0]["transition_in"]["params"]["unknown"] = 1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
}

TEST_CASE("render plan V3 exposes every registered built-in layer transition") {
    const std::vector<std::string> transition_ids{
        "none", "crossfade", "slide", "zoom", "wipe_linear", "smooth_wipe",
        "circle_iris", "flash", "procedural_remotion", "remotion"};
    for (const auto& id : transition_ids) {
        auto source = render_plan_test::base_plan(nlohmann::json::array({
            {{"id", "transition-layer"}, {"type", "color"},
             {"color", {1.0, 1.0, 1.0, 1.0}},
             {"transition_in", {{"kind", id}, {"duration_frames", 1}}}}
        }));
        const auto decoded = chronon3d::render_plan::decode_render_plan(source);
        INFO("built-in transition id: " << id);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->layers[0].transition_in.has_value());
        CHECK(decoded->layers[0].transition_in->kind == id);
    }
}

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

TEST_CASE("V3 shape Field2D decoding is bounded, deterministic, and fingerprinted") {

    using nlohmann::json;
    auto input = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "rect"}, {"fill", {0.8, 0.4, 0.2, 1.0}}})});
    input["layers"][0]["shape"]["fill"] = {
        {"type", "field"},
        {"field", {{"generator", "fractal"}, {"seed", 41}, {"frequency", 6.0},
            {"octaves", 5}, {"operators", json::array({
                {{"kind", "warp"}, {"amount", 0.15}},
                {{"kind", "smoothstep"}, {"edge", 0.55}, {"softness", 0.2}}})}}},
        {"field_ramp", {{"type", "linear"}, {"start", json::array({0.0, 0.5})},
            {"end", json::array({1.0, 0.5})}, {"color_stops", json::array({
                json{{"position", 0.0}, {"color", json::array({0.02, 0.01, 0.1, 1.0})}},
                json{{"position", 0.45}, {"color", json::array({0.45, 0.05, 0.8, 1.0})}},
                json{{"position", 1.0}, {"color", json::array({1.0, 0.7, 0.9, 1.0})}}})}}},
        {"field_render_scale", 4}, {"field_drift", {0.1, -0.05}}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].shape);
    REQUIRE(decoded->layers[0].shape->field);
    CHECK(decoded->layers[0].shape->field->generator ==
          chronon3d::graphics::Field2DGenerator::Fractal);
    CHECK(decoded->layers[0].shape->field->seed == 41);
    CHECK(decoded->layers[0].shape->field->operators.size() == 2);
    REQUIRE(decoded->layers[0].shape->field_ramp);
    CHECK(decoded->layers[0].shape->field_ramp->color_stops.size() == 3);
    CHECK(decoded->layers[0].shape->field_render_scale == 4);
    REQUIRE(decoded->layers[0].shape->field_drift);
    CHECK(decoded->layers[0].shape->field_drift->x == doctest::Approx(0.1f));
    CHECK(decoded->layers[0].shape->field_drift->y == doctest::Approx(-0.05f));

    auto different_seed = input;
    different_seed["layers"][0]["shape"]["fill"]["field"]["seed"] = 42;
    const auto other = chronon3d::render_plan::decode_render_plan(different_seed);
    REQUIRE(other);
    CHECK(decoded->content_fingerprint != other->content_fingerprint);
    auto different_ramp = input;
    different_ramp["layers"][0]["shape"]["fill"]["field_ramp"]["color_stops"][1]["color"][1] = 0.2;
    const auto other_ramp = chronon3d::render_plan::decode_render_plan(different_ramp);
    REQUIRE(other_ramp);
    CHECK(decoded->content_fingerprint != other_ramp->content_fingerprint);
    auto hdr_ramp = input;
    hdr_ramp["layers"][0]["shape"]["fill"]["field_ramp"]["color_stops"][1]["color"][0] = 4.0;
    CHECK(chronon3d::render_plan::decode_render_plan(hdr_ramp));
    hdr_ramp["layers"][0]["shape"]["fill"]["field_ramp"]["color_stops"][1]["color"][0] = 4.01;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(hdr_ramp));
    auto different_scale = input;
    different_scale["layers"][0]["shape"]["fill"]["field_render_scale"] = 2;
    const auto other_scale = chronon3d::render_plan::decode_render_plan(different_scale);
    REQUIRE(other_scale);
    CHECK(decoded->content_fingerprint != other_scale->content_fingerprint);
    auto different_drift = input;
    different_drift["layers"][0]["shape"]["fill"]["field_drift"][0] = 0.2;
    const auto other_drift = chronon3d::render_plan::decode_render_plan(different_drift);
    REQUIRE(other_drift);
    CHECK(decoded->content_fingerprint != other_drift->content_fingerprint);

    auto invalid_scale = input;
    invalid_scale["layers"][0]["shape"]["fill"]["field_render_scale"] = 3;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_scale));

    auto ramp_without_field = input;
    ramp_without_field["layers"][0]["shape"]["fill"].erase("field");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(ramp_without_field));

    auto invalid = input;
    invalid["layers"][0]["shape"]["fill"]["field"]["operators"][0]["amount"] = 2.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));

    auto burst = input;
    burst["layers"][0]["shape"]["fill"]["field"] = {
        {"generator", "burst"}, {"frequency", 1.0}, {"octaves", 8}};
    const auto decoded_burst = chronon3d::render_plan::decode_render_plan(burst);
    REQUIRE(decoded_burst);
    REQUIRE(decoded_burst->layers[0].shape->field);
    CHECK(decoded_burst->layers[0].shape->field->generator ==
          chronon3d::graphics::Field2DGenerator::Burst);
    auto invalid_burst = burst;
    invalid_burst["layers"][0]["shape"]["fill"]["field"]["octaves"] = 2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_burst));
}

TEST_CASE("V3 Field2D can drive displacement without a layer map") {
    using nlohmann::json;
    auto layer = render_plan_test::shape_layer({{"type", "rect"}});
    layer["effects"] = json::array({{{"type", "displacement_map"},
        {"field", {{"generator", "simplex"}, {"seed", 12}, {"frequency", 3.0}}},
        {"intensity_x", 0.1}, {"intensity_y", -0.2}}});
    auto input = render_plan_test::base_plan({layer});
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 1);
    CHECK(decoded->layers[0].effects[0].map_field.has_value());
    CHECK(decoded->layers[0].effects[0].map_layer.empty());

    auto changed = input;
    changed["layers"][0]["effects"][0]["field"]["seed"] = 13;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(decoded->content_fingerprint != changed_plan->content_fingerprint);

    auto conflict = input;
    conflict["layers"][0]["effects"][0]["map_layer"] = "map";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(conflict));
    auto missing_source = input;
    missing_source["layers"][0]["effects"][0].erase("field");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(missing_source));
}

TEST_CASE("V3 Field2D configures the fractal-noise effect sampler") {
    using nlohmann::json;
    auto layer = render_plan_test::shape_layer({{"type", "rect"}});
    layer["effects"] = json::array({{{"type", "fractal_noise"},
        {"field", {{"generator", "cellular"}, {"seed", 7}, {"frequency", 5.0}}}}});
    auto input = render_plan_test::base_plan({layer});
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 1);
    CHECK(decoded->layers[0].effects[0].noise_field.has_value());
    auto changed = input;
    changed["layers"][0]["effects"][0]["field"]["seed"] = 8;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(decoded->content_fingerprint != changed_plan->content_fingerprint);
}

TEST_CASE("V3 Field2D mask stack entries decode and affect content fingerprints") {
    using nlohmann::json;
    auto layer = render_plan_test::shape_layer({{"type", "rect"}});
    layer["masks"] = json::array({{{"type", "field"}, {"mode", "add"},
        {"size", {40.0, 30.0}},
        {"field", {{"generator", "radial"}, {"seed", 8}}}}});
    auto input = render_plan_test::base_plan({layer});
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].masks.size() == 1);
    REQUIRE(decoded->layers[0].masks[0].field);
    auto changed = input;
    changed["layers"][0]["masks"][0]["field"]["seed"] = 9;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(changed_plan->content_fingerprint != decoded->content_fingerprint);
}

TEST_CASE("V3 text mask sources resolve only to existing text layers") {
    using nlohmann::json;
    auto target = render_plan_test::shape_layer({{"type", "rect"}});
    target["masks"] = json::array({{{"type", "text"}, {"mode", "add"},
                                    {"source", "label"}}});
    auto label = json{{"id", "label"}, {"type", "text"}, {"text", "mask"},
        {"style", {{"font", "fonts/roboto.ttf"}, {"font_size", 32.0},
                   {"fill", "#FFFFFF"}}}};
    auto other_label = label;
    other_label["id"] = "other_label";
    const auto input = render_plan_test::base_plan({target, label, other_label});
    const auto decoded = chronon3d::render_plan::decode_render_plan(input);
    const auto decode_error = decoded ? std::string{} :
        decoded.error().path + ": " + decoded.error().message;
    REQUIRE_MESSAGE(decoded, decode_error);
    CHECK(decoded->layers[0].masks[0].text_source == "label");

    auto changed = input;
    changed["layers"][0]["masks"][0]["source"] = "other_label";
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(changed_plan->content_fingerprint != decoded->content_fingerprint);

    auto missing = input;
    missing["layers"][0]["masks"][0]["source"] = "missing";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(missing));

    auto wrong_type = input;
    wrong_type["layers"][1]["type"] = "color";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(wrong_type));

    auto self = input;
    self["layers"][0]["masks"][0]["source"] = self["layers"][0]["id"];
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(self));

    auto cycle = input;
    cycle["layers"][1]["masks"] = json::array({{{"type", "text"},
        {"mode", "add"}, {"source", "shape"}}});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(cycle));
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

TEST_CASE("V3 brush strokes decode texture, linear color ramp, and deterministic profile") {
    auto source = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "path"},
        {"path", {{{"type", "move_to"}, {"point", {0, 0}}},
                   {{"type", "line_to"}, {"point", {20, 0}}}}},
        {"stroke", {{"width", 8}, {"color", {1, 0.5, 0.25, 1}},
            {"brush", {{"size", 12}, {"hardness", 0.8}, {"opacity", 0.7},
                {"flow", 0.5}, {"spacing", 0.2}, {"grain", 0.3},
                {"texture", {{"generator", "linear"}, {"frequency", 2.0}}},
                {"along_path_gradient", render_plan_test::gradient()}}}}}
    })});
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) INFO(decoded.error().path << ": " << decoded.error().message);
    REQUIRE(decoded);
    const auto& profile = *decoded->layers[0].shape->stroke->brush;
    REQUIRE(profile.texture);
    CHECK(profile.texture->generator == chronon3d::graphics::Field2DGenerator::Linear);
    REQUIRE(profile.along_path_gradient);
    CHECK(profile.along_path_gradient->type == chronon3d::graphics::GradientType::Linear);
    const auto repeated = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeated);
    CHECK(decoded->content_fingerprint == repeated->content_fingerprint);

    auto unsupported_gradient = source;
    unsupported_gradient["layers"][0]["shape"]["stroke"]["brush"]["along_path_gradient"]["type"] = "radial";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(unsupported_gradient));
    auto bad_pressure = source;
    bad_pressure["layers"][0]["shape"]["stroke"]["brush"]["pressure_curve"] = {{0.8, 0.4}, {0.2, 0.9}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_pressure));
    auto imported = source;
    imported["layers"][0]["shape"] = {{"type", "path"}, {"asset", "shapes/icon.svg"},
        {"stroke", {{"width", 8}, {"brush", {{"size", 12}}}}}};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(imported));
    auto no_base_width = source;
    no_base_width["layers"][0]["shape"]["stroke"]["width"] = 0;
    no_base_width["layers"][0]["shape"]["stroke"]["brush"]["size"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(no_base_width));

    auto brush_size_supplies_width = source;
    brush_size_supplies_width["layers"][0]["shape"]["stroke"].erase("width");
    const auto size_only = chronon3d::render_plan::decode_render_plan(brush_size_supplies_width);
    REQUIRE(size_only);
    CHECK(size_only->layers[0].shape->stroke->brush->size == doctest::Approx(12.0f));
    CHECK_FALSE(size_only->layers[0].shape->stroke->width.has_value());

    auto negative_base_width = source;
    negative_base_width["layers"][0]["shape"]["stroke"]["width"] = -1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(negative_base_width));

    auto closed_solid = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "path"},
        {"path", {{{"type", "move_to"}, {"point", {0, 0}}},
                   {{"type", "line_to"}, {"point", {20, 0}}},
                   {{"type", "line_to"}, {"point", {10, 20}}},
                   {{"type", "close"}}}},
        {"stroke", {{"width", 4}, {"taper_start", 0.4}}}
    })});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(closed_solid));
    closed_solid["layers"][0]["shape"]["stroke"].erase("taper_start");
    REQUIRE(chronon3d::render_plan::decode_render_plan(closed_solid));

    auto closed_brush = closed_solid;
    closed_brush["layers"][0]["shape"]["stroke"]["brush"] = {{"size", 8}};
    REQUIRE(chronon3d::render_plan::decode_render_plan(closed_brush));
    closed_brush["layers"][0]["shape"]["stroke"]["brush"]["taper_end"] = 0.5;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(closed_brush));

    auto open_brush_with_stroke_taper = source;
    open_brush_with_stroke_taper["layers"][0]["shape"]["stroke"]["taper_start"] = 0.5;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(open_brush_with_stroke_taper));
}

TEST_CASE("Backdrop blur is accepted only on shape layers with bounded parameters") {
    auto shape = render_plan_test::shape_layer({{"type", "rounded_rect"}});
    shape["style"] = {{"backdrop", {{"blur", 16.0}, {"brightness", 1.0}, {"saturation", 1.0},
        {"rim_light", 0.65}, {"refraction", 1.25}, {"surface_noise", 0.025}}}};
    auto plan = render_plan_test::base_plan({shape});
    auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].style->backdrop);
    CHECK(decoded->layers[0].style->backdrop->blur == doctest::Approx(16.0f));
    CHECK(decoded->layers[0].style->backdrop->rim_light == doctest::Approx(0.65f));
    CHECK(decoded->layers[0].style->backdrop->refraction == doctest::Approx(1.25f));
    CHECK(decoded->layers[0].style->backdrop->surface_noise == doctest::Approx(0.025f));
    for (const auto& [field, value] : std::array<std::pair<const char*, double>, 3>{{
             {"rim_light", 2.1}, {"refraction", 4.1}, {"surface_noise", 0.11}}}) {
        auto invalid = plan;
        invalid["layers"][0]["style"]["backdrop"][field] = value;
        CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid));
    }
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

TEST_CASE("RenderPlan image crop decodes normalized rectangles and rejects invalid bounds") {
    using namespace chronon3d;
    auto image = nlohmann::json{{"id", "archive"}, {"type", "image"},
        {"asset", "archive.jpg"}, {"size", {400, 220}}, {"fit", "cover"},
        {"crop", {{"enabled", true}, {"origin", {0.14, 0.08}},
                  {"size", {0.72, 0.84}}, {"flip_x", true}}}};
    auto source = render_plan_test::base_plan({image});
    const auto decoded = render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].crop);
    CHECK(decoded->layers[0].crop->enabled);
    CHECK(decoded->layers[0].crop->origin[0] == doctest::Approx(0.14f));
    CHECK(decoded->layers[0].crop->origin[1] == doctest::Approx(0.08f));
    CHECK(decoded->layers[0].crop->size[0] == doctest::Approx(0.72f));
    CHECK(decoded->layers[0].crop->size[1] == doctest::Approx(0.84f));
    CHECK(decoded->layers[0].crop->flip_x);

    auto out_of_bounds = source;
    out_of_bounds["layers"][0]["crop"]["origin"] = {0.8, 0.2};
    CHECK_FALSE(render_plan::decode_render_plan(out_of_bounds));
    auto on_text = source;
    on_text["layers"][0]["type"] = "text";
    on_text["layers"][0].erase("asset");
    on_text["layers"][0]["text"] = "archive";
    on_text["layers"][0]["crop"] = {{"origin", {0.0, 0.0}}, {"size", {1.0, 1.0}}};
    CHECK_FALSE(render_plan::decode_render_plan(on_text));
}

TEST_CASE("RenderPlan camera DOF channels decode and enforce their ranges") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({{
        {"id", "dof-background"}, {"type", "color"},
        {"size", {320, 180}}, {"color", {0, 0, 0, 1}}
    }}));
    source["canvas"]["duration_frames"] = 54;
    source["camera"] = {{"type", "perspective"}, {"position", {0, 0, -1000}},
        {"rotation_deg", {0, 0, 0}}, {"fov_deg", 50}, {"near", 1}, {"far", 10000},
        {"zoom", 1}, {"dof", {
        {"enabled", true}, {"focus_distance", 0.001}, {"aperture", 0.18}, {"max_blur", 24}}}};
    source["camera_animation"] = {{"tracks", nlohmann::json::array({
        {{"property", "camera_focus_distance"}, {"keyframes", nlohmann::json::array({
            {{"frame", 0}, {"value", 0.001}}, {{"frame", 53}, {"value", 80.0}}})}},
        {{"property", "camera_aperture"}, {"keyframes", nlohmann::json::array({
            {{"frame", 0}, {"value", 0.18}}, {{"frame", 53}, {"value", 0.18}}})}}
    })}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    if (!decoded) MESSAGE(decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->camera_animation);
    CHECK(decoded->camera_animation->size() == 2);
    CHECK(decoded->camera_animation->at(0).property ==
          chronon3d::render_plan::CameraPropertyPlan::FocusDistance);
    CHECK(decoded->camera_animation->at(1).property ==
          chronon3d::render_plan::CameraPropertyPlan::Aperture);

    auto dof_disabled = source;
    dof_disabled["camera"]["dof"]["enabled"] = false;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(dof_disabled));
    auto invalid_aperture = source;
    invalid_aperture["camera_animation"]["tracks"][1]["keyframes"][0]["value"] = 1.1;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(invalid_aperture));
}
