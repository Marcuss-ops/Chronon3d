#include <doctest/doctest.h>

#include "utils/video/variant_batch.hpp"

#include <cstddef>
#include <string>
#include <vector>

TEST_CASE("Variant DAG shares base, layout, and audio stages by fingerprint") {
    using chronon3d::cli::OutputVariant;
    const std::vector<OutputVariant> variants{
        {1920, 1080, "nv12", "h264", "it.mp4", "visual-v1", "layout-it", "audio-it"},
        {1920, 1080, "nv12", "h264", "en.mp4", "visual-v1", "layout-en", "audio-en"},
        {1280, 720, "nv12", "h264", "it-small.mp4", "visual-v1", "layout-it", "audio-it"}};
    const auto plan = chronon3d::cli::plan_variant_dag(variants);
    REQUIRE(plan.outputs.size() == 3);
    CHECK(plan.outputs[0].base_source_index == 0);
    CHECK(plan.outputs[1].base_source_index == 0);
    CHECK(plan.outputs[2].base_source_index == 0);
    CHECK(plan.outputs[2].layout_source_index == 0);
    CHECK(plan.outputs[2].audio_source_index == 0);

    int base_calls = 0;
    int layout_calls = 0;
    int audio_calls = 0;
    std::vector<std::size_t> submitted;
    const bool ok = chronon3d::cli::execute_variant_dag<int, int>(
        plan,
        [&](std::size_t) { ++base_calls; return 100; },
        [&](int base, std::size_t source) {
            ++layout_calls;
            return base + static_cast<int>(source);
        },
        [&](std::size_t source) { ++audio_calls; return static_cast<int>(source); },
        [&](std::size_t index, int, int) { submitted.push_back(index); return true; });
    CHECK(ok);
    CHECK(base_calls == 1);
    CHECK(layout_calls == 2);
    CHECK(audio_calls == 2);
    CHECK(submitted == std::vector<std::size_t>{0, 1, 2});
}

TEST_CASE("Variant DAG fails closed on empty stage fingerprints") {
    const std::vector<chronon3d::cli::OutputVariant> variants{
        {1920, 1080, "nv12", "h264"}, {1920, 1080, "nv12", "h264"}};
    const auto plan = chronon3d::cli::plan_variant_dag(variants);
    REQUIRE(plan.outputs.size() == 2);
    CHECK(plan.outputs[0].base_key != plan.outputs[1].base_key);
    CHECK(plan.outputs[0].layout_key != plan.outputs[1].layout_key);
    CHECK(plan.outputs[0].audio_key != plan.outputs[1].audio_key);
}
