#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>

#include <utility>
#include <limits>
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
            {{"id", "bg_color"}, {"type", "color"}, {"color", {0.1, 0.2, 0.3, 1.0}}},
            {{"id", "img_01"}, {"type", "image"}, {"asset", "portrait.png"}, {"size", {100.0, 100.0}}},
            {{"id", "vid_01"}, {"type", "video"}, {"source", "clips/intro.mp4"}},
            {{"id", "txt_01"}, {"type", "text"}, {"text", "Hello"},
             {"style", {{"font", "fonts/roboto.ttf"}, {"font_size", 32.0}, {"fill", "#FFFFFF"}}}}
        })},
        {"output", {{"path", "out.mp4"}}}};

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    CHECK(decoded->layers.size() == 4);
    CHECK(decoded->layers[0].type == chronon3d::render_plan::LayerType::Color);
    CHECK(decoded->layers[1].type == chronon3d::render_plan::LayerType::Image);
    CHECK(decoded->layers[2].type == chronon3d::render_plan::LayerType::Video);
    CHECK(decoded->layers[3].type == chronon3d::render_plan::LayerType::Text);
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
    REQUIRE(layer.style->background.has_value());
    CHECK(layer.style->background->radius.value() == doctest::Approx(12.0f));
    CHECK(layer.style->background->padding[0] == doctest::Approx(24.0f));

    REQUIRE(layer.animation.has_value());
    REQUIRE(layer.animation->tracks.size() == 1);
    CHECK(layer.animation->tracks[0].property == "opacity");
    CHECK(layer.animation->tracks[0].keyframes.size() == 2);
}
