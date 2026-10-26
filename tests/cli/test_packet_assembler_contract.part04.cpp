#include "test_packet_assembler_contract_test_support.hpp"

TEST_CASE("VariantBatch shares preparation and chooses scale or recompose") {
    const std::vector<chronon3d::cli::OutputVariant> landscape{
        {1920, 1080, "nv12", "h264"},
        {1280, 720, "nv12", "h264"},
        {960, 540, "nv12", "h264"}};
    const auto scale = chronon3d::cli::plan_variant_batch(landscape);
    CHECK(scale.mode == chronon3d::cli::VariantReuseMode::MasterScale);
    CHECK(scale.share_decode);
    CHECK(scale.share_shaping);
    CHECK(scale.share_compilation);
    REQUIRE(scale.executions.size() == landscape.size());
    CHECK(scale.executions[0].source_variant_index == 0);
    CHECK_FALSE(scale.executions[0].scale_master);
    CHECK(scale.executions[1].scale_master);
    CHECK_FALSE(scale.executions[1].recompose_layout);

    const std::vector<chronon3d::cli::OutputVariant> mixed{
        {1920, 1080, "nv12", "h264"},
        {1080, 1920, "nv12", "h264"}};
    const auto recompose = chronon3d::cli::plan_variant_batch(mixed);
    CHECK(recompose.mode ==
          chronon3d::cli::VariantReuseMode::SharedResourcesRecompose);
    REQUIRE(recompose.executions.size() == mixed.size());
    CHECK(recompose.executions[1].source_variant_index == 0);
    CHECK(recompose.executions[1].recompose_layout);
}

TEST_CASE("VariantBatch stops submitting after the first output failure") {
    const std::vector<chronon3d::cli::OutputVariant> variants{
        {1920, 1080, "nv12", "h264"}, {1280, 720, "nv12", "h264"},
        {960, 540, "nv12", "h264"}};
    const auto plan = chronon3d::cli::plan_variant_batch(variants);
    int render_calls = 0;
    std::vector<std::size_t> submitted;
    const bool ok = chronon3d::cli::execute_variant_batch<int>(
        plan,
        [&](std::size_t) { ++render_calls; return 3; },
        [](int master, std::size_t) { return master; },
        [](int master, std::size_t) { return master; },
        [&](std::size_t index, int) {
            submitted.push_back(index);
            return index != 1;
        });
    CHECK_FALSE(ok);
    CHECK(render_calls == 1);
    CHECK(submitted == std::vector<std::size_t>{0, 1});
}

TEST_CASE("VariantBatch renders one master and submits deterministic variants") {
    const std::vector<chronon3d::cli::OutputVariant> variants{
        {1920, 1080, "nv12", "h264"}, {1280, 720, "nv12", "h264"}};
    const auto plan = chronon3d::cli::plan_variant_batch(variants);
    int render_calls = 0;
    int scale_calls = 0;
    int recompose_calls = 0;
    std::vector<int> submitted;
    const bool ok = chronon3d::cli::execute_variant_batch<int>(
        plan,
        [&](std::size_t) { ++render_calls; return 7; },
        [&](int master, std::size_t index) {
            ++scale_calls; return master + static_cast<int>(index);
        },
        [&](int master, std::size_t index) {
            ++recompose_calls; return master - static_cast<int>(index);
        },
        [&](std::size_t index, int) { submitted.push_back(static_cast<int>(index)); return true; });
    CHECK(ok);
    CHECK(render_calls == 1);
    CHECK(scale_calls == 1);
    CHECK(recompose_calls == 0);
    CHECK(submitted == std::vector<int>{0, 1});
}
