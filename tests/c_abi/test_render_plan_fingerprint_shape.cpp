#include "test_render_plan_shape_helpers.hpp"

#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

using namespace chronon3d;

namespace {

std::uint64_t legacy_fingerprint() {
    auto decoded = render_plan::decode_render_plan(render_plan_test::v2_legacy_plan());
    if (!decoded) INFO(decoded.error().message);
    REQUIRE(decoded);
    return decoded->content_fingerprint;
}

}  // namespace

TEST_CASE("V3 shape fingerprint is stable and changes when a shape is added") {
    auto empty_result = render_plan::decode_render_plan(render_plan_test::base_plan());
    REQUIRE(empty_result);
    auto empty = std::move(empty_result).value();
    auto with_shape_json = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"}})});
    auto with_shape_result = render_plan::decode_render_plan(with_shape_json);
    REQUIRE(with_shape_result);
    auto with_shape = std::move(with_shape_result).value();
    CHECK(empty.content_fingerprint ==
          render_plan::compute_render_plan_content_fingerprint(empty));
    CHECK(with_shape.content_fingerprint ==
          render_plan::compute_render_plan_content_fingerprint(with_shape));
    CHECK(with_shape.content_fingerprint != empty.content_fingerprint);
    const auto repeated = render_plan::decode_render_plan(with_shape_json);
    REQUIRE(repeated);
    CHECK(with_shape.content_fingerprint == repeated->content_fingerprint);
}

TEST_CASE("Shape fill, rounded radius, gradient stops, stroke, and path affect fingerprint") {
    auto baseline = render_plan_test::base_plan({
        render_plan_test::shape_layer({
            {"type", "rounded_rect"}, {"radius", 6.0},
            {"fill", {0.1, 0.2, 0.3, 1.0}},
            {"stroke", {{"color", {1.0, 1.0, 1.0, 1.0}}, {"width", 2.0},
                         {"trim_start", 0.1}, {"trim_end", 0.9}}}})});
    const auto decoded_baseline = render_plan::decode_render_plan(baseline);
    REQUIRE(decoded_baseline);
    const auto hash = decoded_baseline->content_fingerprint;
    const std::vector<std::pair<std::string, nlohmann::json>> changes{
        {"shape type", [](auto value) { value["layers"][0]["shape"]["type"] = "rect"; return value; }(baseline)},
        {"radius", [](auto value) { value["layers"][0]["shape"]["radius"] = 7.0; return value; }(baseline)},
        {"fill", [](auto value) { value["layers"][0]["shape"]["fill"][0] = 0.11; return value; }(baseline)},
        {"stroke width", [](auto value) { value["layers"][0]["shape"]["stroke"]["width"] = 3.0; return value; }(baseline)},
        {"stroke trim", [](auto value) { value["layers"][0]["shape"]["stroke"]["trim_end"] = 0.8; return value; }(baseline)}
    };
    for (const auto& [field, changed] : changes) {
        INFO("changed field: " << field);
        const auto decoded = render_plan::decode_render_plan(changed);
        REQUIRE(decoded);
        CHECK(decoded->content_fingerprint != hash);
    }

    auto gradient = render_plan_test::base_plan({
        render_plan_test::shape_layer({
            {"type", "rect"}, {"fill", render_plan_test::gradient()}})});
    const auto decoded_gradient = render_plan::decode_render_plan(gradient);
    REQUIRE(decoded_gradient);
    const auto gradient_hash = decoded_gradient->content_fingerprint;
    gradient["layers"][0]["shape"]["fill"]["color_stops"][0]["color"][0] = 0.4;
    auto changed_stop = render_plan::decode_render_plan(gradient);
    REQUIRE(changed_stop);
    CHECK(changed_stop->content_fingerprint != gradient_hash);

    gradient = render_plan_test::base_plan({
        render_plan_test::shape_layer({
            {"type", "rect"}, {"fill", render_plan_test::gradient()}})});
    gradient["layers"][0]["shape"]["fill"]["spread"] = "repeat";
    const auto spread_changed = render_plan::decode_render_plan(gradient);
    REQUIRE(spread_changed);
    CHECK(spread_changed->content_fingerprint != gradient_hash);
    gradient["layers"][0]["shape"]["fill"]["spread"] = "reflect";
    gradient["layers"][0]["shape"]["fill"]["opacity_stops"][1]["opacity"] = 0.7;
    const auto opacity_changed = render_plan::decode_render_plan(gradient);
    REQUIRE(opacity_changed);
    CHECK(opacity_changed->content_fingerprint != gradient_hash);

    auto path = render_plan_test::base_plan({
        render_plan_test::shape_layer({
            {"type", "path"},
            {"path", {{{"type", "move_to"}, {"point", {0, 0}}},
                       {{"type", "line_to"}, {"point", {20, 20}}}}}})});
    const auto decoded_path = render_plan::decode_render_plan(path);
    REQUIRE(decoded_path);
    const auto path_hash = decoded_path->content_fingerprint;
    path["layers"][0]["shape"]["path"][1]["point"][0] = 21;
    const auto changed_path = render_plan::decode_render_plan(path);
    REQUIRE(changed_path);
    CHECK(changed_path->content_fingerprint != path_hash);
}

TEST_CASE("Per-corner radii affect the render plan fingerprint") {
    auto plan = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "rounded_rect"}, {"corner_radius", {8.0, 8.0, 0.0, 8.0}}})});
    auto first = render_plan::decode_render_plan(plan);
    REQUIRE(first);
    plan["layers"][0]["shape"]["corner_radius"][2] = 2.0;
    auto second = render_plan::decode_render_plan(plan);
    REQUIRE(second);
    CHECK(first->content_fingerprint != second->content_fingerprint);
}

TEST_CASE("Multiple shadows affect the render plan fingerprint") {
    auto plan = render_plan_test::base_plan({render_plan_test::shape_layer({{"type", "rect"}})});
    plan["layers"][0]["style"] = {{"shadows", nlohmann::json::array({
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}, {"blur", 8.0}}})}};
    auto one = render_plan::decode_render_plan(plan);
    if (!one) INFO(one.error().message);
    REQUIRE(one);
    plan["layers"][0]["style"]["shadows"].push_back(
        {{"color", "#000000"}, {"offset", {0.0, 0.0}}, {"blur", 24.0}});
    auto two = render_plan::decode_render_plan(plan);
    REQUIRE(two);
    CHECK(one->content_fingerprint != two->content_fingerprint);
}

TEST_CASE("Backdrop blur settings affect the render plan fingerprint") {
    auto plan = render_plan_test::base_plan({render_plan_test::shape_layer({{"type", "rounded_rect"}})});
    plan["layers"][0]["style"] = {{"backdrop", {{"blur", 16.0}, {"brightness", 1.0}, {"saturation", 1.0},
        {"rim_light", 0.4}, {"refraction", 1.0}, {"surface_noise", 0.02}}}};
    auto first = render_plan::decode_render_plan(plan);
    REQUIRE(first);
    for (const auto& [field, value] : std::array<std::pair<const char*, double>, 3>{{
             {"rim_light", 0.8}, {"refraction", 2.0}, {"surface_noise", 0.04}}}) {
        auto changed = plan;
        changed["layers"][0]["style"]["backdrop"][field] = value;
        auto second = render_plan::decode_render_plan(changed);
        REQUIRE(second);
        CHECK(first->content_fingerprint != second->content_fingerprint);
    }
}

TEST_CASE("Repeater parameters affect render plan fingerprint") {
    auto plan = render_plan_test::base_plan({render_plan_test::shape_layer({
        {"type", "path"},
        {"path", {{{"type", "move_to"}, {"point", {0, 0}}},
                   {{"type", "line_to"}, {"point", {8, 0}}}}},
        {"operators", {{{"kind", "repeater"},
                        {"params", {{"copies", 3}, {"position", {4, 0}}}}}}}})});
    const auto decoded = render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    const auto fingerprint = decoded->content_fingerprint;
    plan["layers"][0]["shape"]["operators"][0]["params"]["copies"] = 4;
    const auto changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != fingerprint);

    plan["layers"][0]["shape"]["operators"] =
        {{{"kind", "round_corners"}, {"params", {{"radius", 2.0}}}}};
    const auto rounded = render_plan::decode_render_plan(plan);
    REQUIRE(rounded);
    const auto rounded_fingerprint = rounded->content_fingerprint;
    plan["layers"][0]["shape"]["operators"][0]["params"]["radius"] = 3.0;
    const auto larger_radius = render_plan::decode_render_plan(plan);
    REQUIRE(larger_radius);
    CHECK(larger_radius->content_fingerprint != rounded_fingerprint);

    plan["layers"][0]["shape"]["operators"] =
        {{{"kind", "zig_zag"}, {"params", {{"size", 2.0}, {"points_per_unit", 0.2}}}}};
    const auto zigzag = render_plan::decode_render_plan(plan);
    REQUIRE(zigzag);
    const auto zigzag_fingerprint = zigzag->content_fingerprint;
    plan["layers"][0]["shape"]["operators"][0]["params"]["size"] = 3.0;
    const auto larger_zigzag = render_plan::decode_render_plan(plan);
    REQUIRE(larger_zigzag);
    CHECK(larger_zigzag->content_fingerprint != zigzag_fingerprint);

    plan["layers"][0]["shape"]["operators"] =
        {{{"kind", "offset_path"}, {"params", {{"amount", 2.0}, {"miter_limit", 3.0}}}}};
    const auto offset = render_plan::decode_render_plan(plan);
    REQUIRE(offset);
    const auto offset_fingerprint = offset->content_fingerprint;
    plan["layers"][0]["shape"]["operators"][0]["params"]["amount"] = 4.0;
    const auto wider_offset = render_plan::decode_render_plan(plan);
    REQUIRE(wider_offset);
    CHECK(wider_offset->content_fingerprint != offset_fingerprint);

    plan["layers"][0]["shape"]["operators"] =
        {{{"kind", "pucker_bloat"}, {"params", {{"amount", 0.5}, {"anchor", {1.0, 2.0}}}}}};
    const auto pucker = render_plan::decode_render_plan(plan);
    REQUIRE(pucker);
    const auto pucker_fingerprint = pucker->content_fingerprint;
    plan["layers"][0]["shape"]["operators"][0]["params"]["anchor"][1] = 3.0;
    const auto shifted_anchor = render_plan::decode_render_plan(plan);
    REQUIRE(shifted_anchor);
    CHECK(shifted_anchor->content_fingerprint != pucker_fingerprint);

    plan["layers"][0]["shape"]["operators"] =
        {{{"kind", "twist"}, {"params", {{"degrees", 45.0}, {"center", {1.0, 2.0}}}}}};
    const auto twist = render_plan::decode_render_plan(plan);
    REQUIRE(twist);
    const auto twist_fingerprint = twist->content_fingerprint;
    plan["layers"][0]["shape"]["operators"][0]["params"]["degrees"] = 90.0;
    const auto stronger_twist = render_plan::decode_render_plan(plan);
    REQUIRE(stronger_twist);
    CHECK(stronger_twist->content_fingerprint != twist_fingerprint);

    plan["layers"][0]["shape"]["operators"] =
        {{{"kind", "wiggle_path"}, {"params", {{"amount", 2.0}, {"frequency", 0.75}, {"seed", 1234}}}}};
    const auto wiggle = render_plan::decode_render_plan(plan);
    REQUIRE(wiggle);
    const auto wiggle_fingerprint = wiggle->content_fingerprint;
    plan["layers"][0]["shape"]["operators"][0]["params"]["seed"] = 4321;
    const auto other_seed = render_plan::decode_render_plan(plan);
    REQUIRE(other_seed);
    CHECK(other_seed->content_fingerprint != wiggle_fingerprint);

    plan["layers"][0]["shape"]["operators"] = nlohmann::json::parse(R"JSON([
      {"kind":"merge","params":{"paths":[[
        {"type":"move_to","point":[10,10]},
        {"type":"line_to","point":[12,12]}
      ]]}}
    ])JSON");
    const auto compound = render_plan::decode_render_plan(plan);
    REQUIRE(compound);
    const auto compound_fingerprint = compound->content_fingerprint;
    plan["layers"][0]["shape"]["operators"][0]["params"]["paths"][0][1]["point"][0] = 13.0;
    const auto changed_compound = render_plan::decode_render_plan(plan);
    REQUIRE(changed_compound);
    CHECK(changed_compound->content_fingerprint != compound_fingerprint);
}

TEST_CASE("Trim animation payload affects render plan fingerprint") {
    auto plan = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"shape","type":"shape","size":[32,32],"shape":{
        "type":"path","path":[{"type":"move_to","point":[0,0]},
          {"type":"line_to","point":[8,0]}],
        "operators":[{"kind":"trim","params":{"start":0,"end":1,
          "animation":{"keyframes":[{"frame":0,"value":[0,0]},
                                       {"frame":10,"value":[0,0.75]}]}}}]}}],
      "output":{"path":"out.png"}
    })JSON");
    const auto initial = render_plan::decode_render_plan(plan);
    REQUIRE(initial);
    const auto fingerprint = initial->content_fingerprint;
    plan["layers"][0]["shape"]["operators"][0]["params"]["animation"]["keyframes"][1]["value"][1] = 0.5;
    const auto changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != fingerprint);
}

TEST_CASE("Spring track and keyframe configs affect render plan fingerprint") {
    auto plan = nlohmann::json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"spring","type":"shape","size":[8,8],"shape":{"type":"rect"},
        "animation":{"tracks":[{"property":"position_y","easing":"spring",
          "spring":{"mass":1,"stiffness":300,"damping":12},
          "keyframes":[{"frame":0,"value":-300,"easing":"spring",
            "spring":{"mass":1,"stiffness":250,"damping":18}},
            {"frame":10,"value":0}]}]}}],
      "output":{"path":"out.png"}
    })JSON");
    const auto initial = render_plan::decode_render_plan(plan);
    REQUIRE(initial);
    const auto fingerprint = initial->content_fingerprint;
    plan["layers"][0]["animation"]["tracks"][0]["spring"]["stiffness"] = 301;
    const auto changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != fingerprint);
    plan["layers"][0]["animation"]["tracks"][0]["spring"]["stiffness"] = 300;
    const auto restored = render_plan::decode_render_plan(plan);
    REQUIRE(restored);
    const auto track_config_fingerprint = restored->content_fingerprint;
    plan["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["spring"]["stiffness"] = 251;
    const auto changed_key_config = render_plan::decode_render_plan(plan);
    REQUIRE(changed_key_config);
    CHECK(changed_key_config->content_fingerprint != track_config_fingerprint);
}

TEST_CASE("Path mask geometry and softness affect render plan fingerprint") {
    auto plan = render_plan_test::base_plan(nlohmann::json::parse(R"JSON([
      {"id":"masked","type":"color","color":[1,1,1,1],"mask":{
        "type":"path","path":[{"type":"move_to","point":[-4,-4]},
          {"type":"line_to","point":[4,-4]},{"type":"line_to","point":[4,4]},
          {"type":"line_to","point":[-4,4]},{"type":"close"}],
        "feather":2,"expansion":1}}
    ])JSON"));
    const auto decoded = render_plan::decode_render_plan(plan);
    const std::string decode_error = decoded.has_value() ? std::string{} : decoded.error().message;
    INFO(decode_error);
    REQUIRE(decoded);
    const auto fingerprint = decoded->content_fingerprint;
    plan["layers"][0]["mask"]["feather"] = 3.0;
    const auto changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != fingerprint);
}

TEST_CASE("LUT asset and amount affect render plan fingerprint") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "graded"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
         {"effects", {{{"type", "lut_3d"}, {"asset", "looks/cinema.cube"}, {"amount", 1.0}}}}}
    }));
    const auto first = render_plan::decode_render_plan(plan);
    REQUIRE(first.has_value());
    const auto fingerprint = first->content_fingerprint;
    plan["layers"][0]["effects"][0]["amount"] = 0.5;
    const auto changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed.has_value());
    CHECK(changed->content_fingerprint != fingerprint);
}

TEST_CASE("Keying controls affect render plan fingerprint") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "keyed"}, {"type", "color"}, {"color", {0.2, 0.7, 0.1, 1.0}},
         {"effects", {{{"type", "keying"}, {"mode", "chroma"},
                       {"key_color", {0.0, 1.0, 0.0}}, {"threshold", 0.22},
                       {"softness", 0.1}, {"spill", 0.8}}}}}
    }));
    const auto first = render_plan::decode_render_plan(plan);
    REQUIRE(first);
    const auto fingerprint = first->content_fingerprint;
    plan["layers"][0]["effects"][0]["threshold"] = 0.25;
    const auto changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != fingerprint);
}

TEST_CASE("Threshold and Mosaic controls affect render plan fingerprint") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "styled"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
         {"effects", {{{"type", "threshold"}, {"threshold", 0.4}},
                       {{"type", "mosaic"}, {"block_size", 8}}}}}
    }));
    const auto first = render_plan::decode_render_plan(plan);
    REQUIRE(first);
    const auto fingerprint = first->content_fingerprint;
    plan["layers"][0]["effects"][1]["block_size"] = 9;
    const auto changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != fingerprint);
}

TEST_CASE("Chromatic Aberration and Halftone controls affect render plan fingerprint") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "styled"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
         {"effects", {{{"type", "chromatic_aberration"}}, {{"type", "halftone"}}}}}
    }));
    const auto first = render_plan::decode_render_plan(plan);
    REQUIRE(first);
    const auto baseline = first->content_fingerprint;
    plan["layers"][0]["effects"][0]["red_offset"] = 6.0;
    auto changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != baseline);
    plan["layers"][0]["effects"][0]["red_offset"] = 2.0;
    plan["layers"][0]["effects"][1]["angle"] = 30.0;
    changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != baseline);
}

TEST_CASE("generated path shape parameters affect the render plan fingerprint") {
    auto plan = render_plan_test::base_plan({render_plan_test::shape_layer(
        {{"type", "star"}, {"points", 5}, {"inner_radius_ratio", 0.5}})});
    const auto original = render_plan::decode_render_plan(plan);
    REQUIRE(original);
    const auto fingerprint = original->content_fingerprint;
    plan["layers"][0]["shape"]["points"] = 7;
    const auto modified = render_plan::decode_render_plan(plan);
    REQUIRE(modified);
    CHECK(modified->content_fingerprint != fingerprint);
}

TEST_CASE("device frame variant and chrome flag affect the render plan fingerprint") {
    auto plan = render_plan_test::base_plan({render_plan_test::shape_layer(
        {{"type", "device_frame"}, {"device", "phone"}, {"chrome", true},
         {"fill", {0.2, 0.3, 0.4, 1.0}}})});
    const auto original = render_plan::decode_render_plan(plan);
    REQUIRE(original);
    const auto fingerprint = original->content_fingerprint;
    plan["layers"][0]["shape"]["chrome"] = false;
    auto modified = render_plan::decode_render_plan(plan);
    REQUIRE(modified);
    CHECK(modified->content_fingerprint != fingerprint);
    plan["layers"][0]["shape"]["chrome"] = true;
    plan["layers"][0]["shape"]["device"] = "browser";
    modified = render_plan::decode_render_plan(plan);
    REQUIRE(modified);
    CHECK(modified->content_fingerprint != fingerprint);
}

TEST_CASE("Directional and Radial Blur controls affect render plan fingerprint") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "blurred"}, {"type", "color"}, {"color", {0.2, 0.3, 0.4, 1.0}},
         {"effects", {{{"type", "directional_blur"}}, {{"type", "radial_blur"}}}}}
    }));
    const auto original = render_plan::decode_render_plan(plan);
    REQUIRE(original);
    const auto fingerprint = original->content_fingerprint;
    plan["layers"][0]["effects"][0]["length"] = 20.0;
    auto modified = render_plan::decode_render_plan(plan);
    REQUIRE(modified);
    CHECK(modified->content_fingerprint != fingerprint);
    plan["layers"][0]["effects"][0]["length"] = 0.0;
    plan["layers"][0]["effects"][1]["amount"] = 0.8;
    modified = render_plan::decode_render_plan(plan);
    REQUIRE(modified);
    CHECK(modified->content_fingerprint != fingerprint);
}

TEST_CASE("Per-key timing fields participate in render-plan fingerprint") {
    auto plan = render_plan_test::base_plan({
        render_plan_test::shape_layer({{"type", "rect"}})});
    plan["layers"][0]["animation"] = nlohmann::json::parse(
        R"({"tracks":[{"property":"opacity","keyframes":[
            {"frame":0,"value":0.0},{"frame":10,"value":1.0}]}]})");
    const auto initial = render_plan::decode_render_plan(plan);
    REQUIRE(initial);
    const auto baseline = initial->content_fingerprint;
    const std::vector<nlohmann::json> changes{
        [](auto p) { p["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["interpolation"] = "hold"; return p; }(plan),
        [](auto p) { p["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["easing"] = "in_quad"; return p; }(plan),
        [](auto p) { auto& k=p["layers"][0]["animation"]["tracks"][0]["keyframes"][0]; k["easing"]="bezier"; k["bezier_curve"]={0.2,0.0,0.8,1.0}; return p; }(plan),
        [](auto p) { p["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["roving"] = true; return p; }(plan),
        [](auto p) { auto& k=p["layers"][0]["animation"]["tracks"][0]["keyframes"][0]; k["interpolation"]="bezier"; k["temporal_out_dx"]=2.0; k["temporal_out_dy"]=0.5; return p; }(plan),
        [](auto p) { p["layers"][0]["animation"]["tracks"][0]["keyframes"][0]["interpolation"] = "auto_bezier"; return p; }(plan)
    };
    for (const auto& changed : changes) {
        const auto decoded = render_plan::decode_render_plan(changed);
        if (!decoded) INFO(decoded.error().message);
        REQUIRE(decoded);
        CHECK(decoded->content_fingerprint != baseline);
    }
}

TEST_CASE("Declared layer transition fields validate and affect fingerprint") {
    auto plan = render_plan_test::base_plan({render_plan_test::shape_layer({{"type", "rect"}})});
    plan["layers"][0]["duration_frames"] = 12;
    plan["layers"][0]["transition_in"] = {
        {"id", "reveal"}, {"kind", "slide"}, {"duration_frames", 6},
        {"delay_frames", 1}, {"easing", "out_cubic"}, {"direction", "left"},
        {"params", {{"distance", 1.25}}}};
    const auto decoded = render_plan::decode_render_plan(plan);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].transition_in);
    CHECK(decoded->layers[0].transition_in->duration_frames == Frame{6});
    const auto baseline = decoded->content_fingerprint;
    plan["layers"][0]["transition_in"]["params"]["distance"] = 1.5;
    const auto changed = render_plan::decode_render_plan(plan);
    REQUIRE(changed);
    CHECK(changed->content_fingerprint != baseline);

    plan["layers"][0]["duration_frames"] = 6;
    CHECK_FALSE(render_plan::decode_render_plan(plan));
    plan["layers"][0]["duration_frames"] = 12;
    plan["layers"][0]["transition_in"]["kind"] = "missing_kind";
    CHECK_FALSE(render_plan::decode_render_plan(plan));
    for (const auto* kind : {"none", "crossfade", "slide", "wipe_linear", "smooth_wipe",
                             "circle_iris", "flash", "procedural_remotion", "remotion"}) {
        auto supported = render_plan_test::base_plan({render_plan_test::shape_layer({{"type", "rect"}})});
        supported["layers"][0]["transition_in"] = {{"kind", kind}, {"duration_frames", 1}};
        CHECK(render_plan::decode_render_plan(supported));
    }
    auto wrong_parameter = render_plan_test::base_plan({render_plan_test::shape_layer({{"type", "rect"}})});
    wrong_parameter["layers"][0]["transition_in"] = {
        {"kind", "crossfade"}, {"duration_frames", 1}, {"params", {{"distance", 2.0}}}};
    CHECK_FALSE(render_plan::decode_render_plan(wrong_parameter));
}

TEST_CASE("Fingerprint v19 domain golden is pinned") {
    // Glass backdrop controls extend the canonical render-plan identity.
    CHECK(legacy_fingerprint() == 3179087526655253714ULL);
}
TEST_CASE("LightRays controls affect render-plan fingerprints") {
    auto plan = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "rays"}, {"type", "color"}, {"color", {0, 0, 0, 1}},
         {"effects", {{{"type", "light_rays"}, {"origin", {0.2, 0.7}},
                       {"length", 240}, {"density", 1.25},
                       {"color", {1, 0.8, 0.6, 0.75}}, {"decay", 0.85}}}}}
    }));
    const auto first = render_plan::decode_render_plan(plan);
    REQUIRE(first);
    const auto fingerprint = first->content_fingerprint;
    const std::vector<std::pair<std::string, nlohmann::json>> mutations{
        {"origin", {0.3, 0.7}}, {"length", 241}, {"density", 1.5},
        {"color", {0.9, 0.8, 0.6, 0.75}}, {"decay", 0.8}
    };
    for (const auto& [field, value] : mutations) {
        auto changed = plan;
        changed["layers"][0]["effects"][0][field] = value;
        const auto decoded = render_plan::decode_render_plan(changed);
        REQUIRE(decoded);
        CHECK(decoded->content_fingerprint != fingerprint);
    }
}
