#include <doctest/doctest.h>

#include "utils/semantic/semantic_script.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace c3d = chronon3d;
namespace semantic = chronon3d::cli::semantic;

namespace {

nlohmann::json make_script() {
    return nlohmann::json::parse(R"({
        "job_id": "first_script_overlay",
        "canvas": { "width": 1920, "height": 1080, "fps": 30, "duration_frames": 300 },
        "background": "images/background.jpg",
        "events": [
            {
                "id": "important_phrase_01",
                "kind": "important_phrase",
                "text": "UNA NUOVA RIVOLUZIONE",
                "start_frame": 30,
                "duration_frames": 60,
                "animation": { "preset": "fade_in" }
            },
            {
                "id": "vision_image",
                "kind": "image_overlay",
                "asset": "images/vision_pro.png",
                "box_width": 700,
                "box_height": 600,
                "position": [430, 0],
                "start_frame": 120,
                "duration_frames": 90
            },
            {
                "id": "important_word_01",
                "kind": "important_word",
                "text": "VISION PRO",
                "start_frame": 120,
                "duration_frames": 60
            }
        ],
        "output": { "path": "output.mp4", "format": "mp4", "codec": "h264", "crf": 18 }
    })");
}

}  // namespace

TEST_CASE("decode_semantic_script parses the three canonical kinds") {
    const auto decoded = semantic::decode_semantic_script(make_script());
    REQUIRE(decoded.has_value());

    const auto& script = decoded.value();
    CHECK(script.job_id == "first_script_overlay");
    CHECK(script.canvas.width == 1920);
    CHECK(script.canvas.duration == c3d::Frame{300});
    REQUIRE(script.background.has_value());
    CHECK(script.background->asset == "images/background.jpg");
    REQUIRE(script.events.size() == 3);

    CHECK(script.events[0].kind == semantic::SemanticKind::ImportantPhrase);
    CHECK(script.events[0].text == "UNA NUOVA RIVOLUZIONE");
    CHECK(script.events[1].kind == semantic::SemanticKind::ImageOverlay);
    CHECK(script.events[1].asset == "images/vision_pro.png");
    CHECK(script.events[2].kind == semantic::SemanticKind::ImportantWord);
    CHECK(script.events[2].text == "VISION PRO");

    CHECK(script.output.format == c3d::render_plan::OutputFormat::Mp4);
    CHECK(script.output.codec == c3d::render_plan::VideoCodec::H264);
}

TEST_CASE("decode_semantic_script rejects an unknown kind") {
    auto root = make_script();
    root["events"][0]["kind"] = "quote";
    const auto decoded = semantic::decode_semantic_script(root);
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error().path == "events[0].kind");
}

TEST_CASE("compile_semantic_script maps kinds onto canonical presets") {
    const auto script = semantic::decode_semantic_script(make_script()).value();
    const auto plan = semantic::compile_semantic_script(script);

    REQUIRE(plan.layers.size() == 4);  // background + 3 events

    // Background is the full-canvas backdrop, emitted first.
    const auto& background = plan.layers[0];
    CHECK(background.id == "background");
    CHECK(background.type == c3d::render_plan::LayerType::Image);
    CHECK(background.asset == "images/background.jpg");
    CHECK(background.fit == c3d::render_plan::FitMode::Cover);
    CHECK(background.start_frame == c3d::Frame{0});
    CHECK(background.duration_frames == c3d::Frame{300});

    const auto& phrase = plan.layers[1];
    CHECK(phrase.type == c3d::render_plan::LayerType::Text);
    CHECK(phrase.preset == "caption_safe_area");
    CHECK(phrase.animation.has_value());
    CHECK(phrase.animation->preset == "fade_in");

    const auto& image = plan.layers[2];
    CHECK(image.type == c3d::render_plan::LayerType::Image);
    CHECK(image.asset == "images/vision_pro.png");
    CHECK(image.fit == c3d::render_plan::FitMode::Contain);
    REQUIRE(image.box_width.has_value());
    CHECK(*image.box_width == 700.0f);
    CHECK(image.position_dimensions == 2);
    CHECK(image.position[0] == 430.0f);

    const auto& word = plan.layers[3];
    CHECK(word.type == c3d::render_plan::LayerType::Text);
    CHECK(word.preset == "kinetic_word");
}

TEST_CASE("compile_semantic_script honors preset and fit overrides") {
    auto root = make_script();
    root["events"][0]["preset"] = "title_centered";
    root["events"][1]["fit"] = "cover";
    const auto script = semantic::decode_semantic_script(root).value();
    const auto plan = semantic::compile_semantic_script(script);

    CHECK(plan.layers[1].preset == "title_centered");
    CHECK(plan.layers[2].fit == c3d::render_plan::FitMode::Cover);
}

TEST_CASE("compile_semantic_script emits a color backdrop when no asset is given") {
    auto root = make_script();
    root["background"] = {{"color", {0.1f, 0.2f, 0.3f, 1.0f}}};
    const auto script = semantic::decode_semantic_script(root).value();
    const auto plan = semantic::compile_semantic_script(script);

    REQUIRE_FALSE(plan.layers.empty());
    CHECK(plan.layers[0].type == c3d::render_plan::LayerType::Color);
    CHECK(plan.layers[0].color[0] == doctest::Approx(0.1f));
}

TEST_CASE("render_plan_to_json round-trips through the canonical decoder") {
    const auto script = semantic::decode_semantic_script(make_script()).value();
    const auto plan = semantic::compile_semantic_script(script);
    const auto json = semantic::render_plan_to_json(plan);

    CHECK(json.at("schema") == "chronon.render-plan");
    CHECK(json.at("version") == 1);

    const auto decoded = c3d::render_plan::decode_render_plan(json);
    REQUIRE(decoded.has_value());
    CHECK(decoded->job_id == "first_script_overlay");
    REQUIRE(decoded->layers.size() == plan.layers.size());
    CHECK(decoded->layers[1].preset == "caption_safe_area");
    CHECK(decoded->layers[3].preset == "kinetic_word");
    CHECK(decoded->output.path == "output.mp4");
}
