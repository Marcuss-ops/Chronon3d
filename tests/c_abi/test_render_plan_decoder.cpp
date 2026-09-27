#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/animation/core/animated_value.hpp>

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
      "layers":[{"id":"card","type":"color","flow":{"group_id":"cards","axis":"row","gap":8,"cell_size":[120,80],"justify":"space_between"}}],
      "output":{"path":"out.png"}
    })JSON");
    const auto decoded=chronon3d::render_plan::decode_render_plan(source);
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
    const auto& keys = timing_plan->layers[0].animation->tracks[0].keyframes;
    CHECK(keys[0].interpolation == "auto_bezier");
    CHECK(keys[1].roving);

    source["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["interpolation"] = "bezier";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
    source["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["interpolation"] = "linear";
    source["layers"][0]["animation"]["tracks"][0]["keyframes"][1]["temporal_in_dx"] = -2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
}

TEST_CASE("per-key temporal handles, roving and AutoBezier preserve AnimatedValue semantics") {
    using namespace chronon3d;

    chronon3d::render_plan::
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
    CHECK(roving.keyframes()[1].frame == Frame{3});
    CHECK(roving.keyframes()[2].frame == Frame{4});
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
        "none", "crossfade", "slide", "wipe_linear", "smooth_wipe",
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
    changed_geometry["layers"][0]["masks"][1]["size"][0] = 44;
    const auto changed_geometry_decoded = chronon3d::render_plan::decode_render_plan(changed_geometry);
    REQUIRE(changed_geometry_decoded.has_value());
    CHECK(changed_geometry_decoded->content_fingerprint != changed->content_fingerprint);
    auto changed_order = source;
    const auto second_mask = changed_order["layers"][0]["masks"][1];
    changed_order["layers"][0]["masks"][1] = changed_order["layers"][0]["masks"][2];
    changed_order["layers"][0]["masks"][2] = second_mask;
    const auto changed_order_decoded = chronon3d::render_plan::decode_render_plan(changed_order);
    REQUIRE(changed_order_decoded.has_value());
    CHECK(changed_order_decoded->content_fingerprint != changed->content_fingerprint);

    source["layers"][0]["fill_gradient_animation"]["keyframes"][1]["value"]["spread"] = "pad";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
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
            {{"type", "rect"}, {"mode", "add"}, {"size", {80, 60}}, {"position", {2, 3}}},
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
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->layers.size() == 1);
    REQUIRE(decoded->layers[0].masks.size() == 4);
    CHECK(decoded->layers[0].masks[0].type == "rect");
    CHECK(decoded->layers[0].masks[1].mode == "intersect");
    CHECK(decoded->layers[0].masks[1].inverted);
    CHECK(decoded->layers[0].masks[2].mode == "subtract");
    CHECK(decoded->layers[0].masks[3].mode == "difference");
    const auto fingerprint = decoded->content_fingerprint;

    source["layers"][0]["masks"][2]["mode"] = "add";
    const auto changed = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(changed.has_value());
    CHECK(changed->content_fingerprint != fingerprint);

    source["layers"][0]["masks"][0]["mode"] = "subtract";
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source).has_value());
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

TEST_CASE("render plan V3 rejects unimplemented primitive and counter payloads") {
    for (const auto* type : {"world_map", "chart", "counter"}) {
        auto source = render_plan_test::base_plan({
            render_plan_test::shape_layer({{"type", type}})});
        CHECK_FALSE(chronon3d::render_plan::decode_render_plan(source));
    }
    auto counter_payload = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"},
            {"counter", {{"from", 0}, {"to", 100}}}})});
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(counter_payload));
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
         {"effects", {{{"type", "chromatic_aberration"}, {"red_offset", 3.5}, {"blue_offset", -4.0}},
                       {{"type", "halftone"}, {"size", 12}, {"angle", 22.5}, {"amount", 0.8}}}}}
    }));
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].effects.size() == 2);
    CHECK(decoded->layers[0].effects[0].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::ChromaticAberration);
    CHECK(decoded->layers[0].effects[0].chromatic_red_offset == doctest::Approx(3.5f));
    CHECK(decoded->layers[0].effects[0].chromatic_blue_offset == doctest::Approx(-4.0f));
    CHECK(decoded->layers[0].effects[1].kind == chronon3d::render_plan::LayerPlan::EffectKindPlan::Halftone);
    CHECK(decoded->layers[0].effects[1].halftone_size == 12);
    plan["layers"][0]["effects"][0]["red_offset"] = 129.0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));
    plan["layers"][0]["effects"][0]["red_offset"] = 2.0;
    plan["layers"][0]["effects"][1]["amount"] = 1.1;
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
                   {"stroke", {{"color", "#FFFFFF"}, {"width", 2.0}}}},
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
        INFO("shape content: " << test.name);
        auto layer = render_plan_test::shape_layer(test.shape);
        auto input = render_plan_test::base_plan({layer});
        const auto decoded = render_plan::decode_render_plan(input);
        if (!decoded) INFO(decoded.error().message);
        REQUIRE(decoded);
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
