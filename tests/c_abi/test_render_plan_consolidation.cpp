#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>

namespace {

nlohmann::json valid_color_plan() {
    return nlohmann::json{
        {"schema", "chronon.render-plan.v2"},
        {"version", 2},
        {"canvas", {
            {"width", 640}, {"height", 360}, {"fps_num", 30}, {"fps_den", 1},
            {"duration_frames", 12}}},
        {"layers", nlohmann::json::array({
            {{"id", "background"}, {"type", "color"},
             {"color", nlohmann::json::array({0.0, 0.0, 0.0, 1.0})}}
        })},
        {"output", {{"path", "out.png"}, {"format", "png"}}}
    };
}

nlohmann::json valid_text_plan() {
    auto plan = valid_color_plan();
    plan["layers"] = nlohmann::json::array({
        {{"id", "title"}, {"type", "text"}, {"text", "Chronon"},
         {"style", {{"font", "fonts/main.ttf"}, {"font_size", 32.0},
                    {"fill", "#FFFFFF"}}}}
    });
    return plan;
}

}  // namespace

TEST_CASE("render plan fingerprint ignores JSON object key order") {
    const auto first = nlohmann::json::parse(R"json(
        {
          "schema":"chronon.render-plan.v2",
          "version":2,
          "canvas":{"width":640,"height":360,"fps_num":30,"fps_den":1,"duration_frames":12},
          "layers":[{"id":"background","type":"color","color":[0.0,0.0,0.0,1.0]}],
          "output":{"path":"out.png","format":"png"}
        }
    )json");
    const auto second = nlohmann::json::parse(R"json(
        {
          "output":{"format":"png","path":"out.png"},
          "layers":[{"color":[0.0,0.0,0.0,1.0],"type":"color","id":"background"}],
          "canvas":{"duration_frames":12,"fps_den":1,"fps_num":30,"height":360,"width":640},
          "version":2,
          "schema":"chronon.render-plan.v2"
        }
    )json");

    const auto a = chronon3d::render_plan::decode_render_plan(first);
    const auto b = chronon3d::render_plan::decode_render_plan(second);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->content_fingerprint == b->content_fingerprint);
}

TEST_CASE("render plan content fingerprint is typed and excludes output settings") {
    auto first = valid_color_plan();
    auto second = first;
    second["output"]["path"] = "different.png";
    second["output"]["format"] = "mp4";
    second["output"]["codec"] = "h264";

    const auto a = chronon3d::render_plan::decode_render_plan(first);
    const auto b = chronon3d::render_plan::decode_render_plan(second);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->content_fingerprint == b->content_fingerprint);

    second["layers"][0]["color"][0] = 0.25;
    const auto changed = chronon3d::render_plan::decode_render_plan(second);
    REQUIRE(changed);
    CHECK(a->content_fingerprint != changed->content_fingerprint);
}

TEST_CASE("render plan diagnostics keep conditional failures under validator authority") {
    auto plan = valid_text_plan();
    plan["layers"][0]["style"].erase("fill");

    const auto result = chronon3d::render_plan::validate_render_plan(plan);
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.issues.empty());
    for (const auto& issue : result.issues) {
        CHECK(issue.kind
              == chronon3d::render_plan::ValidationIssueKind::SchemaViolation);
        CHECK_FALSE(issue.detail.empty());
    }
}

TEST_CASE("render plan diagnostics preserve pattern errors without classification") {
    auto plan = valid_text_plan();
    plan["layers"][0]["style"]["fill"] = "not-a-hex-color";

    const auto result = chronon3d::render_plan::validate_render_plan(plan);
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.issues.empty());
    for (const auto& issue : result.issues) {
        CHECK(issue.kind
              == chronon3d::render_plan::ValidationIssueKind::SchemaViolation);
        CHECK_FALSE(issue.detail.empty());
    }
}
