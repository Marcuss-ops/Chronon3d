#include "commands/render/caption_plan_expansion.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>

TEST_CASE("caption expansion lowers cue timing and estimated word timing to plan") {
    static std::atomic<unsigned> sequence{0};
    const auto root = std::filesystem::temp_directory_path() /
        ("chronon-caption-expansion-test-" + std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directories(root);
    const auto plan_path = root / "plan.json";
    const auto srt_path = root / "captions.srt";
    {
        std::ofstream plan(plan_path);
        plan << R"({"schema":"chronon.render-plan.v3","version":3,"canvas":{"width":640,"height":360,"fps_num":30,"fps_den":1,"duration_frames":120},"layers":[],"output":{"path":"out.mp4"}})";
        std::ofstream srt(srt_path);
        srt << "1\n00:00:01,000 --> 00:00:02,000\nhello world\n";
    }
    auto expanded = chronon3d::cli::expand_caption_plan(
        plan_path, srt_path, "karaoke", "fonts/test.ttf");
    REQUIRE(expanded.error.empty());
    REQUIRE(!expanded.plan_path.empty());
    std::ifstream input(expanded.plan_path);
    const auto json = nlohmann::json::parse(input);
    REQUIRE(json["layers"].size() == 1);
    CHECK(json["layers"][0]["start_frame"] == 30);
    CHECK(json["layers"][0]["duration_frames"] == 30);
    REQUIRE(json["layers"][0]["spans"].size() == 2);
    REQUIRE(json["layers"][0]["text_animators"].size() == 2);
    CHECK(json["layers"][0]["text_animators"][0]["selectors"].size() == 1);
    REQUIRE(json["warnings"].size() == 1);
    CHECK(json["warnings"][0] == "caption_word_timing_estimated");
    const auto decoded = chronon3d::render_plan::decode_render_plan(json);
    if (!decoded) std::cerr << decoded.error().message << '\n';
    REQUIRE(decoded);
    CHECK(decoded->warnings.back() == "caption_word_timing_estimated");
    auto repeated = chronon3d::cli::expand_caption_plan(
        plan_path, srt_path, "karaoke", "fonts/test.ttf");
    REQUIRE(repeated.error.empty());
    std::ifstream repeated_input(repeated.plan_path);
    const auto repeated_json = nlohmann::json::parse(repeated_input);
    const auto repeated_decoded = chronon3d::render_plan::decode_render_plan(repeated_json);
    REQUIRE(repeated_decoded);
    CHECK(decoded->content_fingerprint == repeated_decoded->content_fingerprint);
    for (const auto* preset : {"word_pop", "typewriter"}) {
        auto variant = chronon3d::cli::expand_caption_plan(
            plan_path, srt_path, preset, "fonts/test.ttf");
        REQUIRE(variant.error.empty());
        std::ifstream variant_input(variant.plan_path);
        const auto variant_json = nlohmann::json::parse(variant_input);
        CHECK(chronon3d::render_plan::decode_render_plan(variant_json));
    }
    std::filesystem::remove_all(root);
}
