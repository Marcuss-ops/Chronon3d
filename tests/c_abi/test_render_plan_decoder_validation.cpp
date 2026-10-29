#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>
#include "../../src/render_plan/light_attenuation.hpp"
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

    auto far_bg = base;
    far_bg["layers"][0].erase("position");
    far_bg["layers"][0]["depth_role"] = "far_background";
    auto foreground = far_bg;
    foreground["layers"][0]["depth_role"] = "foreground";
    const auto far_plan = chronon3d::render_plan::decode_render_plan(far_bg);
    const auto near_plan = chronon3d::render_plan::decode_render_plan(foreground);
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
         render_plan::ShapeContentTypePlan::DeviceFrame},
        {"ring_segment", {{"type", "ring_segment"}, {"start_degrees", 0.0},
                            {"sweep_degrees", 180.0}, {"inner_radius_ratio", 0.5},
                            {"segments", 32}, {"fill", {1.0, 0.0, 0.0, 1.0}}},
         render_plan::ShapeContentTypePlan::RingSegment}
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
        if (test.expected == render_plan::ShapeContentTypePlan::RingSegment) {
            REQUIRE(decoded->layers[0].shape->path.size() > 3);
            CHECK(decoded->layers[0].shape->path.back().type ==
                  render_plan::ShapePathCommandTypePlan::Close);
            CHECK(decoded->layers[0].shape->inner_radius_ratio == doctest::Approx(0.5f));
        }
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

    auto ring = render_plan_test::shape_layer({{"type", "ring_segment"},
        {"sweep_degrees", 120.0}, {"inner_radius_ratio", 0.25},
        {"fill", {1.0, 0.0, 0.0, 1.0}}});
    auto ring_plan = render_plan_test::base_plan({ring});
    const auto original_ring = render_plan::decode_render_plan(ring_plan);
    REQUIRE(original_ring);
    ring_plan["layers"][0]["shape"]["sweep_degrees"] = 121.0;
    const auto changed_ring = render_plan::decode_render_plan(ring_plan);
    REQUIRE(changed_ring);
    CHECK(changed_ring->content_fingerprint != original_ring->content_fingerprint);
    auto missing_ring_fill = render_plan_test::shape_layer({{"type", "ring_segment"}});
    CHECK_FALSE(render_plan::decode_render_plan(render_plan_test::base_plan({missing_ring_fill})));
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

TEST_CASE("resolved camera samples carry DOF and physical optics") {
    auto source = render_plan_test::base_plan(nlohmann::json::array({{
        {"id", "camera-background"}, {"type", "color"},
        {"size", {320, 180}}, {"color", {0, 0, 0, 1}}
    }}));
    source["canvas"]["duration_frames"] = 2;
    source["camera"] = {{"type", "perspective"}, {"fov_deg", 50}, {"near", 1}, {"far", 10000},
        {"zoom", 1}, {"focal_length_mm", 50}, {"sensor_width_mm", 32},
        {"lens_shift", {0.04, -0.02}}, {"f_stop", 2.0}, {"iris_blades", 6},
        {"dof", {{"enabled", true}, {"focus_distance", 1400}, {"aperture", 0.02}, {"max_blur", 24},
            {"f_stop", 2.0}, {"iris_blades", 6}, {"highlight_gain", 1.5}}}};
    source["camera_samples"] = nlohmann::json::array({
        {{"frame", 0}, {"camera", {{"focal_length_mm", 50}, {"f_stop", 2.0},
            {"dof", {{"enabled", true}, {"focus_distance", 480}, {"aperture", 0.04}, {"max_blur", 12}}}}}},
        {{"frame", 1}, {"camera", {{"focal_length_mm", 85}, {"f_stop", 2.8},
            {"sensor_width_mm", 24}, {"lens_shift", {-0.1, 0.03}},
            {"dof", {{"enabled", true}, {"focus_distance", 720}, {"aperture", 0.06}, {"max_blur", 20}}}}}}});
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded);
    REQUIRE(decoded->camera_samples);
    CHECK(decoded->camera_samples->at(1).camera.focal_length_mm == doctest::Approx(85.f));
    CHECK(decoded->camera_samples->at(1).camera.sensor_width_mm == doctest::Approx(24.f));
    CHECK(decoded->camera_samples->at(1).camera.lens_shift[0] == doctest::Approx(-0.1f));
    CHECK(decoded->camera_samples->at(1).camera.lens_shift[1] == doctest::Approx(0.03f));
    CHECK(decoded->camera_samples->at(1).camera.dof_focus_distance == doctest::Approx(720.f));
    CHECK(decoded->camera_samples->at(1).camera.dof_aperture == doctest::Approx(0.06f));

    auto bad_order = source;
    bad_order["camera_samples"][1]["frame"] = 0;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_order));
    auto bad_camera = source;
    bad_camera["camera_samples"][1]["camera"]["focal_length_mm"] = 400;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_camera));
    auto bad_sensor = source;
    bad_sensor["camera"]["sensor_width_mm"] = 0.2;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_sensor));
    auto bad_shift = source;
    bad_shift["camera"]["lens_shift"] = {1.5, 0};
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_shift));
    auto bad_iris = source;
    bad_iris["camera"]["dof"]["iris_blades"] = 16;
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(bad_iris));
}
