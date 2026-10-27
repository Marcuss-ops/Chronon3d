#include "test_render_plan_decoder_test_support.hpp"

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

TEST_CASE("camera consumer reads resolved complete samples") {
    auto source = render_plan_test::base_plan();
    source["camera"] = {{"type", "perspective"}, {"position", {0, 0, -1000}},
        {"rotation_deg", {0, 0, 0}}, {"fov_deg", 50}, {"near", 1},
        {"far", 10000}, {"zoom", 1}};
    source["camera_samples"] = nlohmann::json::array({
        {{"frame", 0}, {"camera", {{"position", {10, 30, -500}}, {"rotation_deg", {1, 4, 7}}}}},
        {{"frame", 2}, {"camera", {{"position", {20, 40, -800}}, {"rotation_deg", {3, 6, 9}}}}}});
    const auto plan = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(plan);
    const auto evaluate = chronon3d::render_plan::detail::make_camera_evaluator(*plan);
    REQUIRE(evaluate);
    CHECK(evaluate(0.0).position.x == doctest::Approx(10.f));
    CHECK(evaluate(1.0).position.x == doctest::Approx(15.f));
    CHECK(evaluate(1.0).position.y == doctest::Approx(35.f));
    CHECK(evaluate(1.0).position.z == doctest::Approx(-650.f));
    CHECK(evaluate(2.0).rotation.z == doctest::Approx(9.f));
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
        "environment":true,"depth_range":[0.2,0.8],"bokeh":true,
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
    CHECK(plan->layers[0].particle_emitter->environment);
    CHECK(plan->layers[0].particle_emitter->depth_min == doctest::Approx(0.2f));
    CHECK(plan->layers[0].particle_emitter->depth_max == doctest::Approx(0.8f));
    CHECK(plan->layers[0].particle_emitter->bokeh);
    CHECK(plan->layers[0].particle_emitter->blend_mode == chronon3d::BlendMode::Add);
    REQUIRE(plan->layers[0].particle_emitter->color_ramp.color_stops.size() == 2);

    auto changed = source;
    changed["layers"][0]["particle_emitter"]["seed"] = 4729;
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan.has_value());
    CHECK(plan->content_fingerprint != changed_plan->content_fingerprint);

    changed = source;
    changed["layers"][0]["particle_emitter"]["depth_range"] = {0.3, 0.8};
    const auto changed_depth = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_depth.has_value());
    CHECK(plan->content_fingerprint != changed_depth->content_fingerprint);

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

    changed = source;
    changed["layers"][0]["particle_emitter"]["count"] = 4097;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed).has_value());
    changed = source;
    changed["layers"][0]["particle_emitter"]["depth_range"] = {0.9, 0.2};
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

    auto spot = source;
    spot["layers"][0]["light"] = {
        {"kind", "spot"}, {"radius", 240}, {"color", "#FF8040"},
        {"intensity", 1.5}, {"range", 320}, {"decay", 2},
        {"direction", {0.0, 1.0, 0.0}},
        {"angle", 0.9599}, {"penumbra", 0.2}, {"cast_shadow", true},
        {"shadow_radius", 12}, {"shadow_bias", 0.5}};
    const auto spot_plan = chronon3d::render_plan::decode_render_plan(spot);
    REQUIRE(spot_plan.has_value());
    const auto& spot_light = *spot_plan->layers[0].light;
    CHECK(spot_light.kind == chronon3d::render_plan::LightKindPlan::Spot);
    CHECK(spot_light.range == doctest::Approx(320.0f));
    CHECK(spot_light.decay == doctest::Approx(2.0f));
    CHECK(spot_light.direction[1] == doctest::Approx(1.0f));
    CHECK(chronon3d::render_plan::light_range_attenuation(spot_light, 0.0f) ==
          doctest::Approx(1.0f));
    CHECK(chronon3d::render_plan::light_range_attenuation(spot_light, 160.0f) ==
          doctest::Approx(0.25f));
    CHECK(chronon3d::render_plan::light_range_attenuation(spot_light, 320.0f) ==
          doctest::Approx(0.0f));
    CHECK(spot_light.cast_shadow);
    CHECK(spot_plan->content_fingerprint != plan->content_fingerprint);
    spot["layers"][0]["light"]["decay"] = 9;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(spot).has_value());

    spot["layers"][0]["light"]["decay"] = 2;
    spot["layers"][0]["animation"] = nlohmann::json::parse(R"JSON({
      "tracks": [
        {"property":"light_intensity","keyframes":[
          {"frame":0,"value":0.5},{"frame":23,"value":1.5}]},
        {"property":"light_color","keyframes":[
          {"frame":0,"value":[1.0,0.4,0.2,1.0]},
          {"frame":23,"value":[0.2,0.5,1.0,1.0]}]}
      ]
    })JSON");
    const auto animated_spot = chronon3d::render_plan::decode_render_plan(spot);
    const std::string animation_error = animated_spot.has_value()
        ? std::string{} : animated_spot.error().path + ": " + animated_spot.error().message;
    INFO(animation_error);
    REQUIRE(animated_spot.has_value());
    REQUIRE(animated_spot->layers[0].animation.has_value());
    CHECK(animated_spot->layers[0].animation->tracks.size() == 2);
    spot["layers"][0]["animation"]["tracks"][0]["property"] = "unknown_light_property";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(spot).has_value());
    spot["layers"][0]["animation"]["tracks"][0]["property"] = "light_intensity";
    spot["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["value"] = 17;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(spot).has_value());
    spot["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["value"] = 0.5;
    spot["layers"][0]["animation"]["tracks"].push_back(
        spot["layers"][0]["animation"]["tracks"][0]);
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(spot).has_value());
}

TEST_CASE("light_direction tracks decode and reject invalid vectors and layer kinds") {
    auto source = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":24,"fps_den":1,"duration_frames":24},
      "layers":[{"id":"key","type":"light","light":{"kind":"spot"},
        "animation":{"tracks":[{"property":"light_direction","keyframes":[
          {"frame":0,"value":[0,0,-1]},{"frame":20,"value":[1,0,-1]}]}]}}],
      "output":{"path":"out.png"}})JSON");
    const auto valid = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(valid);
    auto changed = source;
    changed["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["value"] = {0, 1, -1};
    const auto changed_plan = chronon3d::render_plan::decode_render_plan(changed);
    REQUIRE(changed_plan);
    CHECK(changed_plan->content_fingerprint != valid->content_fingerprint);
    for (const auto& invalid : {nlohmann::json::array({0, 0}),
                                nlohmann::json::array({0, 0, 0})}) {
        changed = source;
        changed["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["value"] = invalid;
        CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed));
    }
    changed = source;
    changed["layers"][0]["light"]["kind"] = "radial";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed));
    changed = source;
    changed["layers"][0]["animation"]["tracks"].push_back(
        changed["layers"][0]["animation"]["tracks"][0]);
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed));
    changed = source;
    changed["layers"][0]["type"] = "color";
    changed["layers"][0].erase("light");
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(changed));
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
