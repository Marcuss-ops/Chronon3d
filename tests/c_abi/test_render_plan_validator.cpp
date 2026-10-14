// RenderPlan JSON-Schema validator contract tests.
// nlohmann-json-schema-validator is the only structural authority; Chronon
// tests validation outcome plus preservation of raw validator diagnostics.

#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using chronon3d::render_plan::validate_render_plan;
using chronon3d::render_plan::validate_render_plan_or_throw;
using chronon3d::render_plan::ValidationIssueKind;

namespace {

json make_minimal_valid_plan() {
    return json{
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {
            {"width", 1920},
            {"height", 1080},
            {"fps_num", 30},
            {"fps_den", 1},
            {"duration_frames", 60}
        }},
        {"layers", json::array({
            {
                {"id", "background"},
                {"type", "color"},
                {"color", json::array({0.0, 0.0, 0.0, 1.0})}
            }
        })},
        {"output", {{"path", "out.png"}, {"format", "png"}}}
    };
}

void check_schema_violation(const chronon3d::render_plan::ValidationResult& result,
                            const std::string& context = {}) {
    INFO("context: " << context << "\n" << result.format());
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.issues.empty());
    for (const auto& issue : result.issues) {
        CHECK(issue.kind == ValidationIssueKind::SchemaViolation);
        CHECK(issue.expected == "JSON Schema constraint");
        CHECK_FALSE(issue.detail.empty());
    }
}

}  // namespace

TEST_CASE("render_plan: minimal valid plan passes") {
    const auto result = validate_render_plan(make_minimal_valid_plan());
    INFO(result.format());
    CHECK(result.ok());
    CHECK(result.issues.empty());
}

TEST_CASE("render_plan: CPU light kinds and shadow fields satisfy the schema") {
    auto plan = make_minimal_valid_plan();
    plan["layers"] = json::array({{
        {"id", "key"}, {"type", "light"}, {"position", {100, 80}},
        {"light", {{"kind", "spot"}, {"radius", 240}, {"range", 320},
                    {"decay", 2}, {"direction", {1, 0, 0}},
                    {"angle", 0.9599}, {"penumbra", 0.2},
                    {"intensity", 1.5}, {"color", "#FF8040"},
                    {"cast_shadow", true}, {"shadow_radius", 12},
                    {"shadow_bias", 0.5}}}
    }});
    CHECK(validate_render_plan(plan).ok());
    plan["layers"][0]["light"]["kind"] = "area";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: extra root-level field is rejected by schema authority") {
    auto plan = make_minimal_valid_plan();
    plan["totally_extra_field"] = "drift";
    const auto result = validate_render_plan(plan);
    check_schema_violation(result);
    CHECK_THROWS_AS(validate_render_plan_or_throw(plan), std::runtime_error);
}

TEST_CASE("render_plan: extra layer field is rejected by schema authority") {
    auto plan = make_minimal_valid_plan();
    plan["layers"][0]["typo"] = "ignored-no-more";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: missing required field is rejected without local reconstruction") {
    auto plan = make_minimal_valid_plan();
    plan["output"].erase("path");
    const auto result = validate_render_plan(plan);
    check_schema_violation(result);
    CHECK_THROWS_AS(validate_render_plan_or_throw(plan), std::runtime_error);
}

TEST_CASE("render_plan: wrong type is rejected without local keyword classification") {
    auto plan = make_minimal_valid_plan();
    plan["canvas"]["width"] = "abc";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: nested required-field violation remains a validator diagnostic") {
    auto plan = make_minimal_valid_plan();
    plan["layers"][0].erase("id");
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: multiple validator errors are preserved") {
    auto plan = make_minimal_valid_plan();
    plan["canvas"]["width"] = 0;
    plan["canvas"]["fps_num"] = -1;
    plan["layers"][0]["type"] = "bogus";
    plan["output"].erase("path");
    plan["output"]["crf"] = 999;
    const auto result = validate_render_plan(plan);
    check_schema_violation(result);
    CHECK(result.issues.size() >= 2u);
}

TEST_CASE("render_plan: const mismatch remains an opaque schema diagnostic") {
    auto plan = make_minimal_valid_plan();
    plan["schema"] = "wrong.schema";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: minLength violation remains an opaque schema diagnostic") {
    auto plan = make_minimal_valid_plan();
    plan["output"]["path"] = "";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: non-object root is rejected without crashing") {
    const json array_root = json::array({1, 2, 3});
    check_schema_violation(validate_render_plan(array_root));
}

TEST_CASE("render_plan V3 shape fill is accepted as an extension and checked by base projection") {
    auto plan = json{
        {"schema", "chronon.render-plan.v3"}, {"version", 3},
        {"canvas", {{"width", 40}, {"height", 30}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 2}}},
        {"layers", json::array({
            {{"id", "shape"}, {"type", "shape"}, {"size", {20, 10}},
             {"color", {0.1, 0.2, 0.3, 1.0}},
             {"shape", {{"type", "rect"}, {"fill", {0.8, 0.7, 0.6, 1.0}}}}}
        })},
        {"output", {{"path", "out.png"}}}
    };
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK(result.ok());

    // validate_render_plan is a pure validator over const JSON. Its V2
    // projection copies shape.fill into layer.color and erases shape.fill on a
    // private copy only; the producer JSON must remain untouched.
    CHECK(plan["layers"][0]["color"][0] == 0.1);
    CHECK(plan["layers"][0]["shape"].contains("fill"));
}

TEST_CASE("render_plan V3 GradientMesh fills validate and enforce node bounds") {
    auto plan = make_minimal_valid_plan();
    plan["schema"] = "chronon.render-plan.v3";
    plan["version"] = 3;

    json nodes = json::array({
        {{"position", {0.0, 0.0}}, {"color", {1.0, 0.0, 0.0, 1.0}}},
        {{"position", {1.0, 0.0}}, {"color", {0.0, 1.0, 0.0, 1.0}}},
        {{"position", {0.0, 1.0}}, {"color", {0.0, 0.0, 1.0, 1.0}}},
        {{"position", {1.0, 1.0}}, {"color", {1.0, 1.0, 1.0, 1.0}}}
    });
    json fill = {{"type", "mesh"}};
    fill["mesh"]["nodes"] = std::move(nodes);
    json shape = {{"type", "rect"}};
    shape["fill"] = std::move(fill);
    json layer = {
        {"id", "mesh-shape"}, {"type", "shape"}, {"size", {20, 10}},
        {"shape", std::move(shape)}
    };
    plan["layers"] = json::array({std::move(layer)});

    const auto valid = validate_render_plan(plan);
    INFO(valid.format());
    CHECK(valid.ok());

    plan["layers"][0]["shape"]["fill"]["mesh"]["nodes"][0]["position"][0] = 1.1;
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan V3 text solid stroke remains valid through the shared V2 projection") {
    auto plan = json{
        {"schema", "chronon.render-plan.v3"}, {"version", 3},
        {"canvas", {{"width", 1280}, {"height", 720}, {"fps_num", 24},
                     {"fps_den", 1}, {"duration_frames", 48}}},
        {"layers", json::array({
            {{"id", "caption"}, {"type", "text"}, {"text", "ROME"},
             {"size", {480, 88}}, {"style", {
                 {"font", "Poppins-Bold"}, {"font_size", 24}, {"fill", "#FFFFFF"},
                 {"stroke", {{"color", "#111827"}, {"width", 3.5}}}
             }}}
        })},
        {"output", {{"path", "out.png"}}}
    };
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK(result.ok());
}

TEST_CASE("render_plan V3 rejects unsupported WorldMap, Chart, and shape.counter contracts") {
    const auto valid_shape_plan = [](json shape) {
        return json{
            {"schema", "chronon.render-plan.v3"}, {"version", 3},
            {"canvas", {{"width", 40}, {"height", 30}, {"fps_num", 30},
                         {"fps_den", 1}, {"duration_frames", 12}}},
            {"layers", json::array({{{"id", "shape"}, {"type", "shape"},
                {"size", {20, 10}}, {"shape", std::move(shape)}}})},
            {"output", {{"path", "out.png"}}}
        };
    };
    const std::vector<json> unsupported_shapes = json::parse(R"JSON([
      {"type":"world_map","world_map":{"projection":"mercator","center":[12,41],
        "zoom":3,"route":[[-74,40],[12,41]]}},
      {"type":"chart","chart":{"type":"pie","data":[2,3],"donut":true,
        "donut_inner_ratio":0.5,"value_format":"currency"}},
      {"type":"rect","counter":{"from":0,"to":100}},
      {"type":"rect","world_map":{"route":[[0,0],[1,1]]}},
      {"type":"rect","chart":{"type":"bar","data":[1,2]}}
    ])JSON");
    for (const auto& shape : unsupported_shapes) {
        INFO(shape.dump());
        check_schema_violation(validate_render_plan(valid_shape_plan(shape)));
    }
}

TEST_CASE("render_plan V3 brush strokes validate paths, widths, and taper restrictions") {
    const auto make_shape_plan = [](json shape) {
        return json{
            {"schema", "chronon.render-plan.v3"}, {"version", 3},
            {"canvas", {{"width", 64}, {"height", 64}, {"fps_num", 30},
                         {"fps_den", 1}, {"duration_frames", 12}}},
            {"layers", json::array({{{"id", "paint"}, {"type", "shape"},
                {"size", {64, 64}}, {"shape", std::move(shape)}}})},
            {"output", {{"path", "out.png"}}}
        };
    };
    const auto open_path = json::parse(R"JSON({
      "type":"path","path":[{"type":"move_to","point":[0,0]},
        {"type":"line_to","point":[40,0]}],
      "stroke":{"color":"#FFFFFF","brush":{"size":8,"taper_start":0.2}}
    })JSON");
    CHECK(validate_render_plan(make_shape_plan(open_path)).ok());

    auto missing_width = open_path;
    missing_width["stroke"]["brush"].erase("size");
    check_schema_violation(validate_render_plan(make_shape_plan(missing_width)));
    auto explicit_width = missing_width;
    explicit_width["stroke"]["width"] = 4;
    CHECK(validate_render_plan(make_shape_plan(explicit_width)).ok());
    auto zero_size_inherits_width = explicit_width;
    zero_size_inherits_width["stroke"]["brush"]["size"] = 0;
    CHECK(validate_render_plan(make_shape_plan(zero_size_inherits_width)).ok());
    auto zero_width = open_path;
    zero_width["stroke"]["width"] = 0;
    zero_width["stroke"]["brush"]["size"] = 0;
    check_schema_violation(validate_render_plan(make_shape_plan(zero_width)));
    auto negative_width = open_path;
    negative_width["stroke"]["width"] = -1;
    check_schema_violation(validate_render_plan(make_shape_plan(negative_width)));

    auto closed_path = json::parse(R"JSON({
      "type":"path","path":[{"type":"move_to","point":[0,0]},
        {"type":"line_to","point":[40,0]},{"type":"line_to","point":[20,40]},
        {"type":"close"}],
      "stroke":{"color":"#FFFFFF","width":3}
    })JSON");
    CHECK(validate_render_plan(make_shape_plan(closed_path)).ok());
    closed_path["stroke"]["taper_start"] = 0.2;
    check_schema_violation(validate_render_plan(make_shape_plan(closed_path)));

    closed_path["stroke"].erase("taper_start");
    closed_path["stroke"]["brush"] = {{"size", 8}};
    CHECK(validate_render_plan(make_shape_plan(closed_path)).ok());
    closed_path["stroke"]["brush"]["taper_end"] = 0.8;
    check_schema_violation(validate_render_plan(make_shape_plan(closed_path)));

    auto generated_closed = make_shape_plan({{"type", "star"},
        {"points", 5}, {"stroke", {{"color", "#FFFFFF"},
            {"brush", {{"size", 8}, {"taper_start", 0.2}}}}}});
    check_schema_violation(validate_render_plan(generated_closed));
    auto aliased_dot_grid_taper = make_shape_plan({{"type", "dotgrid"},
        {"stroke", {{"color", "#FFFFFF"},
            {"brush", {{"size", 8}, {"taper_end", 0.5}}}}}});
    check_schema_violation(validate_render_plan(aliased_dot_grid_taper));

    auto competing_gradient = open_path;
    competing_gradient["stroke"]["gradient"] = {
        {"type", "linear"}, {"start", {0, 0}}, {"end", {1, 0}},
        {"color_stops", {{{"position", 0}, {"color", {1, 0, 0, 1}}},
                          {{"position", 1}, {"color", {0, 0, 1, 1}}}}}};
    check_schema_violation(validate_render_plan(make_shape_plan(competing_gradient)));

    auto unsupported_shape = make_shape_plan({{"type", "ellipse"},
        {"stroke", {{"color", "#FFFFFF"}, {"brush", {{"size", 8}}}}}});
    check_schema_violation(validate_render_plan(unsupported_shape));
    auto imported = make_shape_plan({{"type", "path"}, {"asset", "marks/a.svg"},
        {"stroke", {{"color", "#FFFFFF"}, {"brush", {{"size", 8}}}}}});
    check_schema_violation(validate_render_plan(imported));
}

TEST_CASE("render_plan V3 accepts the per-frame paint track family") {
    // Regression: the V3 extension schema documents stroke_width, stroke_color
    // and shadow_<n>_* as animatable, but the frozen V2 projection only erased
    // fill_color/skew/skew_axis, so the base pass rejected every one of those
    // tracks with "instance not found in required enum" and the CLI refused to
    // render a plan the extension schema accepts.
    auto plan = json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":40,"height":30,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[{"id":"ribbon","type":"shape","size":[20,10],
        "color":[0.1,0.2,0.3,1.0],
        "shape":{"type":"path",
          "path":[{"type":"move_to","point":[0,0]}],
          "stroke":{"width":2,"color":"#FFFFFF"}},
        "style":{"shadows":[{"color":"#000000","opacity":0.5,"blur":2,"offset":[1,1]},
                             {"color":"#FFFFFF","opacity":0.25,"offset":[0,2]}]},
        "animation":{"tracks":[
          {"property":"stroke_width","keyframes":[{"frame":0,"value":1.0},{"frame":11,"value":6.0}]},
          {"property":"stroke_color","keyframes":[{"frame":0,"value":[1.0,1.0,1.0,1.0]}]},
          {"property":"stroke_dash_offset","keyframes":[{"frame":0,"value":0.0},{"frame":11,"value":4.0}]},
          {"property":"shadow_0_opacity","keyframes":[{"frame":0,"value":0.0},{"frame":11,"value":0.5}]},
          {"property":"shadow_1_blur","keyframes":[{"frame":0,"value":1.0}]},
          {"property":"shadow_1_offset_y","keyframes":[{"frame":0,"value":0.0},{"frame":11,"value":8.0}]},
          {"property":"shadow_1_color","keyframes":[{"frame":0,"value":[0.0,0.0,0.0,1.0]}]}
        ]}}],
      "output":{"path":"out.png"}})JSON");
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK(result.ok());

    // The vocabulary stays closed: an unknown property is still rejected, and
    // so is a shadow index past the schema's three-shadow bound.
    plan["layers"][0]["animation"]["tracks"][0]["property"] = "shear";
    check_schema_violation(validate_render_plan(plan));
    plan["layers"][0]["animation"]["tracks"][0]["property"] = "shadow_3_blur";
    check_schema_violation(validate_render_plan(plan));

    // The frozen V2 enum is unchanged: the same paint tracks remain invalid
    // there, which is what makes the projection necessary at all.
    plan["layers"][0]["animation"]["tracks"][0]["property"] = "stroke_width";
    plan["schema"] = "chronon.render-plan.v2";
    plan["version"] = 2;
    plan["layers"][0].erase("shape");
    plan["layers"][0].erase("style");
    plan["layers"][0]["type"] = "color";
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan V3 particle emitter extensions survive frozen V2 projection") {
    auto plan = json::parse(R"JSON({
      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":64,"height":36,"fps_num":30,"fps_den":1,"duration_frames":12},
      "layers":[
        {"id":"chalk-path","type":"shape","size":[64,36],
         "shape":{"type":"path","path":[
           {"type":"move_to","point":[4,18]},
           {"type":"line_to","point":[60,18]}]}},
        {"id":"chalk-dust","type":"particles","size":[64,36],
         "particle_emitter":{"count":24,"seed":319,"emitter_size":[4,2],
           "velocity_min":[-1,-8],"velocity_max":[1,-2],"lifetime":[0.2,0.8],
           "size":[0.5,2.0],"shape":"path","path_id":"chalk-path",
           "path_points":[[4,18],[60,18]],"sprite_asset":"",
           "sprite_scale":[0.8,1.2],"rotation":[-0.5,0.5],
           "angular_velocity":[-1,1]}}
      ],
      "output":{"path":"out.png"}
    })JSON");
    const auto result = validate_render_plan(plan);
    INFO(result.format());
    CHECK(result.ok());
}

TEST_CASE("render_plan V3 exposes typed transition parameters and excludes them from frozen V2") {
    auto plan = json{
        {"schema", "chronon.render-plan.v3"}, {"version", 3},
        {"canvas", {{"width", 40}, {"height", 30}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 12}}},
        {"layers", json::array({
            {{"id", "shape"}, {"type", "color"}, {"color", {0.1, 0.2, 0.3, 1.0}},
             {"transition_in", {{"kind", "slide"}, {"duration_frames", 12},
                {"params", {{"distance", 0.75}}}}}}
        })},
        {"output", {{"path", "out.png"}}}
    };
    CHECK(validate_render_plan(plan).ok());

    plan["layers"][0]["transition_in"]["params"]["distance"] = 101;
    check_schema_violation(validate_render_plan(plan));
    plan["layers"][0]["transition_in"]["params"] = {{"unexpected", 1}};
    check_schema_violation(validate_render_plan(plan));

    plan["schema"] = "chronon.render-plan.v2";
    plan["version"] = 2;
    plan["layers"][0].erase("transition_in");
    plan["layers"][0]["animation"] = {{"tracks", json::array({
        {{"property", "fill_color"}, {"keyframes", json::array({
            {{"frame", 0}, {"value", {1.0, 0.0, 0.0, 1.0}}}
        })}}
    })}};
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan V3 schema constrains LightRays controls and adjustment payloads") {
    auto plan = json{
        {"schema", "chronon.render-plan.v3"}, {"version", 3},
        {"canvas", {{"width", 40}, {"height", 30}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 2}}},
        {"layers", json::array({
            {{"id", "rays"}, {"type", "color"}, {"color", {0, 0, 0, 1}},
             {"effects", json::array({{{"type", "light_rays"}, {"origin", {0.25, 0.75}},
               {"length", 100000}, {"density", 8}, {"color", {1, 1, 1, 1}}, {"decay", 1}}})}}
        })},
        {"output", {{"path", "out.png"}}}
    };
    CHECK(validate_render_plan(plan).ok());
    const std::vector<std::pair<std::string, json>> invalid_rays{
        {"origin", json::array({-0.01, 0.5})}, {"origin", json::array({0.5, 1.01})},
        {"length", 100001}, {"density", 8.01}, {"decay", -0.01},
        {"color", json::array({1, 0, 0, 1.01})}
    };
    for (const auto& [field, value] : invalid_rays) {
        auto invalid = plan;
        invalid["layers"][0]["effects"][0][field] = value;
        INFO("invalid LightRays field: " << field);
        check_schema_violation(validate_render_plan(invalid));
    }
    auto v2_light_rays = plan;
    v2_light_rays["schema"] = "chronon.render-plan.v2";
    v2_light_rays["version"] = 2;
    check_schema_violation(validate_render_plan(v2_light_rays));

    plan["layers"] = json::array({
        {{"id", "adjust"}, {"type", "adjustment"},
         {"effects", json::array({{{"type", "noise"}}})}}
    });
    CHECK(validate_render_plan(plan).ok());

    auto safe_insets = make_minimal_valid_plan();
    safe_insets["schema"] = "chronon.render-plan.v3";
    safe_insets["version"] = 3;
    safe_insets["layers"][0]["safe_insets"] = {
        {"top", 24}, {"right", 12}, {"bottom", 32}, {"left", 12}};
    const auto accepted_insets = validate_render_plan(safe_insets);
    INFO("accepted V3 safe_insets\n" << accepted_insets.format());
    CHECK(accepted_insets.ok());

    auto clip_composite = json::parse(R"JSON({

      "schema":"chronon.render-plan.v3","version":3,
      "canvas":{"width":1280,"height":720,"fps_num":30,"fps_den":1,"duration_frames":90},
      "layers":[
        {"id":"source","type":"video","source":"assets/source.mp4","size":[1280,720],"fit":"cover","start_frame":0,"duration_frames":90},
        {"id":"clip__grid","type":"shape","size":[1280,720],"position":[0,0],"screen_space":true,"opacity":0.2,"start_frame":0,"duration_frames":90,
         "shape":{"type":"grid","spacing":64,"stroke":{"color":"#33CCDD","width":1}}},
        {"id":"clip__adjustment","type":"adjustment","effects":[
          {"type":"wave_warp","amplitude":0.0125,"wavelength":0.035,"speed":0.0,"direction":[0,1],"wave_type":"square"},
          {"type":"chromatic_aberration","red_offset":4.0,"blue_offset":-4.0},
          {"type":"noise","amount":0.06,"size":1.0,"seed":7,"animated":true,"color_mode":"rgb"}
        ]}
      ],
      "output":{"path":"out.mp4","format":"mp4","codec":"h264"}
    })JSON");
    CHECK(validate_render_plan(clip_composite).ok());
    auto bad_grid = clip_composite;
    bad_grid["layers"][1]["shape"]["spacing"] = 0.0;
    check_schema_violation(validate_render_plan(bad_grid));
    auto bad_effect = clip_composite;
    bad_effect["layers"][2]["effects"][0]["wavelength"] = 0;
    check_schema_violation(validate_render_plan(bad_effect));
    for (const auto* field : {"screen_space", "enable_3d", "opacity", "blend_mode",
                              "safe_insets", "depth_role", "depth_offset", "radius",
                              "start_frame", "duration_frames", "fit", "loop",
                              "transition_in", "transition_out"}) {
        auto invalid = plan;
        if (std::string(field) == "safe_insets") invalid["layers"][0][field] = {{"unexpected", 1}};
        else if (std::string(field) == "depth_role" || std::string(field) == "fit" ||
                 std::string(field) == "blend_mode") invalid["layers"][0][field] = "value";
        else if (std::string(field) == "transition_in" || std::string(field) == "transition_out")
            invalid["layers"][0][field] = {{"id", "none"}};
        else invalid["layers"][0][field] = 1;
        check_schema_violation(validate_render_plan(invalid),
                               std::string("forbidden adjustment field: ") + field);
    }
}

TEST_CASE("render_plan V3 schema rejects malformed path fields and gradient stops") {
    auto plan = json{
        {"schema", "chronon.render-plan.v3"}, {"version", 3},
        {"canvas", {{"width", 40}, {"height", 30}, {"fps_num", 30},
                     {"fps_den", 1}, {"duration_frames", 2}}},
        {"layers", json::array({
            {{"id", "shape"}, {"type", "shape"}, {"size", {20, 10}},
             {"shape", {{"type", "path"}, {"path", {{{"type", "move_to"}, {"point", {0}}}}}}}}
        })},
        {"output", {{"path", "out.png"}}}
    };
    check_schema_violation(validate_render_plan(plan));

    plan["layers"][0]["shape"] = {
        {"type", "rect"},
        {"fill", {{"type", "linear"},
                  {"color_stops", {{{"position", 0.5}, {"color", {1, 0, 0, 1}}}}}}}
    };
    check_schema_violation(validate_render_plan(plan));
}

TEST_CASE("render_plan: validator instance pointers are converted but never inferred") {
    auto plan = make_minimal_valid_plan();
    plan["layers"][0]["animation"] = {
        {"tracks", json::array({
            {{"property", "invalid_property"}, {"keyframes", json::array({
                {{"frame", 0}, {"value", 1.0}}
            })}}
        })}
    };
    const auto result = validate_render_plan(plan);
    check_schema_violation(result);
    bool saw_nested_path = false;
    for (const auto& issue : result.issues) {
        if (issue.path.find("layers[0]") != std::string::npos) {
            saw_nested_path = true;
            break;
        }
    }
    CHECK(saw_nested_path);
}
