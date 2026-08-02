#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>

#include <nlohmann/json.hpp>

#include <utility>

TEST_CASE("render plan decoder constructs typed V1 plan") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan"},
        {"version", 1},
        {"canvas", {{"width", 1920}, {"height", 1080}, {"fps", 30},
                     {"duration_frames", 60}}},
        {"layers", {{{"id", "title"}, {"type", "text"},
                      {"text", "Hello"}, {"start_frame", 3}}}},
        {"audio_tracks", {{{"source", "music.wav"}, {"volume", 0.5}}}},
        {"output", {{"path", "out.mp4"}, {"format", "mp4"},
                     {"codec", "h264"}}}};

    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(decoded.has_value());
    CHECK(decoded->canvas.width == 1920);
    CHECK(decoded->layers.size() == 1);
    CHECK(decoded->layers[0].type == chronon3d::render_plan::LayerType::Text);
    CHECK(decoded->layers[0].start_frame->integral() == 3);
    CHECK(decoded->audio_tracks[0].volume == doctest::Approx(0.5));
    CHECK(decoded->output.codec == chronon3d::render_plan::VideoCodec::H264);
}

TEST_CASE("render plan decoder returns validation path on malformed plan") {
    const nlohmann::json source = {{"schema", "chronon.render-plan"}};
    const auto decoded = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE_FALSE(decoded.has_value());
    CHECK_FALSE(decoded.error().message.empty());
}

TEST_CASE("render plan decoder rejects absolute and traversal asset references") {
    const nlohmann::json base = {
        {"schema", "chronon.render-plan"},
        {"version", 1},
        {"canvas", {{"width", 320}, {"height", 180}, {"fps", 30},
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
    plan.canvas.duration = chronon3d::Frame{10};
    plan.layers.resize(2);

    chronon3d::render_plan::RenderBudget budget;
    budget.max_layers = 1;
    const auto error = chronon3d::render_plan::validate_render_plan_budget(plan, budget);
    REQUIRE(error.has_value());
    CHECK(error->path == "layers");
}

TEST_CASE("render plan fingerprint ignores JSON formatting") {
    const auto compact = nlohmann::json::parse(
        R"({"schema":"chronon.render-plan","version":1,"canvas":{"width":320,"height":180,"fps":30,"duration_frames":1},"layers":[{"id":"title","type":"text","text":"Hello"}],"output":{"path":"out.png"}})");
    const auto formatted = nlohmann::json::parse(R"(
    {
      "output": { "path": "out.png" },
      "layers": [ { "text": "Hello", "type": "text", "id": "title" } ],
      "canvas": { "duration_frames": 1, "fps": 30, "height": 180, "width": 320 },
      "version": 1,
      "schema": "chronon.render-plan"
    })");
    const auto compact_plan = chronon3d::render_plan::decode_render_plan(compact);
    const auto formatted_plan = chronon3d::render_plan::decode_render_plan(formatted);
    REQUIRE(compact_plan);
    REQUIRE(formatted_plan);
    CHECK(compact_plan->content_fingerprint == formatted_plan->content_fingerprint);
}

TEST_CASE("render plan fingerprint includes decoded content and preserves order") {
    const nlohmann::json source = {
        {"schema", "chronon.render-plan"},
        {"version", 1},
        {"canvas", {{"width", 640}, {"height", 360}, {"fps", 30},
                     {"duration_frames", 12}}},
        {"layers", {{{"id", "first"}, {"type", "text"},
                      {"text", "one"}},
                     {{"id", "second"}, {"type", "text"},
                      {"text", "two"}}}},
        {"output", {{"path", "out.png"}}}};

    const auto original = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(original.has_value());
    CHECK(original->content_fingerprint ==
          chronon3d::render_plan::compute_render_plan_content_fingerprint(*original));

    const auto repeat = chronon3d::render_plan::decode_render_plan(source);
    REQUIRE(repeat.has_value());
    CHECK(original->content_fingerprint == repeat->content_fingerprint);

    auto changed_text = source;
    changed_text["layers"][0]["text"] = "changed";
    const auto changed = chronon3d::render_plan::decode_render_plan(changed_text);
    REQUIRE(changed.has_value());
    CHECK(original->content_fingerprint != changed->content_fingerprint);

    auto reordered = source;
    std::swap(reordered["layers"][0], reordered["layers"][1]);
    const auto reordered_plan = chronon3d::render_plan::decode_render_plan(reordered);
    REQUIRE(reordered_plan.has_value());
    CHECK(original->content_fingerprint != reordered_plan->content_fingerprint);
}
