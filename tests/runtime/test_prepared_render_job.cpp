#include <doctest/doctest.h>

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/math/color.hpp>
#include <tests/helpers/test_utils.hpp>

#include <chrono>
#include <algorithm>
#include <thread>
#include <vector>

namespace {

chronon3d::Composition make_empty_composition() {
    return chronon3d::Composition{
        chronon3d::CompositionSpec{
            .name = "prepared-render-job-contract",
            .width = 64,
            .height = 64,
            .frame_rate = {30, 1},
            .duration = chronon3d::Frame{2}},
        [](const chronon3d::FrameContext&) { return chronon3d::Scene{}; }};
}

} // namespace

TEST_CASE("PreparedRenderJob compiles and prepares once, then renders compiled frames") {
    chronon3d::RenderEngine engine;
    chronon3d::PreparedRenderJobOptions options;
    options.node_cache_capacity_bytes = 16u * 1024u * 1024u;
    options.pipeline_depth = 3;
    auto job = engine.prepare(make_empty_composition(), options);

    CHECK(job.frame_count() == chronon3d::Frame{2});
    CHECK(job.resource_plan().requests.size() >= 3);
    CHECK(job.resource_plan().slots.size() >= 3);
    const auto graph_request_count = std::count_if(
        job.resource_plan().requests.begin(), job.resource_plan().requests.end(),
        [](const auto& request) { return request.id.starts_with("GraphNode["); });
    CHECK(graph_request_count > 0);
    CHECK(job.resource_plan().peak_live_bytes > 0);
    CHECK(job.resource_plan().planned_physical_bytes >=
          job.resource_plan().peak_live_bytes);
    const auto prepared = job.telemetry();
    CHECK(prepared.cache_capacity_bytes > 0);
    CHECK(prepared.pipeline_depth == 3);
    CHECK(prepared.pipeline_in_flight == 0);
    CHECK(prepared.framebuffer_allocations > 0);
    CHECK(job.render(chronon3d::Frame{0}));
    CHECK(job.render(chronon3d::Frame{1}));
    const auto rendered = job.telemetry();
    CHECK(rendered.nodes_executed >= prepared.nodes_executed);
    CHECK(rendered.pipeline_in_flight == 0);
    CHECK(rendered.framebuffer_allocations >= prepared.framebuffer_allocations);

    job.finish();
    CHECK_THROWS(static_cast<void>(job.render(chronon3d::Frame{0})));
}

TEST_CASE("PreparedRenderJob rejects a depth different from its fixed triple ring") {
    chronon3d::RenderEngine engine;
    chronon3d::PreparedRenderJobOptions options;
    options.pipeline_depth = 2;
    CHECK_THROWS(static_cast<void>(engine.prepare(make_empty_composition(), options)));
}

TEST_CASE("PreparedRenderJob split evaluation preserves framebuffer output") {
    const chronon3d::Composition composition{
        chronon3d::CompositionSpec{
            .name = "prepared-render-job-output",
            .width = 96,
            .height = 64,
            .frame_rate = {30, 1},
            .duration = chronon3d::Frame{1}},
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder scene(ctx.resource);
            scene.rect("rect", {
                .size = {48.0f, 32.0f},
                .color = chronon3d::Color{0.2f, 0.7f, 0.9f, 1.0f},
            });
            return scene.build();
        }};

    chronon3d::RenderEngine prepared_engine;
    chronon3d::RenderEngine convenience_engine;
    auto job = prepared_engine.prepare(composition);
    const auto prepared_frame = job.render(chronon3d::Frame{0});
    const auto convenience_frame = convenience_engine.render(composition, chronon3d::Frame{0});

    REQUIRE(prepared_frame != nullptr);
    REQUIRE(convenience_frame != nullptr);
    CHECK(chronon3d::test::framebuffer_hash(*prepared_frame) ==
          chronon3d::test::framebuffer_hash(*convenience_frame));
}

TEST_CASE("PreparedRenderJob render_frames applies bounded encoder backpressure") {
    chronon3d::RenderEngine engine;
    auto composition = make_empty_composition();
    auto job = engine.prepare(composition);
    std::vector<chronon3d::Frame> encoded;

    const auto result = job.render_frames(
        chronon3d::Frame{0},
        chronon3d::Frame{8},
        [&](chronon3d::Frame frame, const chronon3d::Framebuffer&) {
            encoded.push_back(frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return true;
        });

    REQUIRE(result.ok);
    CHECK(result.frames_rendered == 8);
    CHECK(result.frames_encoded == 8);
    CHECK(result.max_queue_depth <= 3);
    REQUIRE(encoded.size() == 8);
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        CHECK(encoded[i] == chronon3d::Frame{static_cast<chronon3d::i64>(i)});
    }
    CHECK(job.telemetry().pipeline_in_flight == 0);
}

TEST_CASE("PreparedRenderJob framebuffer allocations plateau after preparation") {
    const chronon3d::Composition composition{
        chronon3d::CompositionSpec{
            .name = "prepared-render-job-plateau",
            .width = 32,
            .height = 32,
            .frame_rate = {30, 1},
            .duration = chronon3d::Frame{300}},
        [](const chronon3d::FrameContext&) { return chronon3d::Scene{}; }};

    chronon3d::RenderEngine engine;
    auto job = engine.prepare(composition);
    const auto prepared = job.telemetry();
    for (int frame = 0; frame < 300; ++frame) {
        REQUIRE(job.render(chronon3d::Frame{frame}));
    }
    const auto completed = job.telemetry();

    CHECK(completed.framebuffer_allocations == prepared.framebuffer_allocations);
    CHECK(completed.framebuffer_bytes_allocated == prepared.framebuffer_bytes_allocated);
    CHECK(completed.pipeline_in_flight == 0);
}

TEST_CASE("PreparedRenderJob render_frames propagates encoder failure without deadlock") {
    chronon3d::RenderEngine engine;
    auto job = engine.prepare(make_empty_composition());

    const auto result = job.render_frames(
        chronon3d::Frame{0},
        chronon3d::Frame{8},
        [](chronon3d::Frame frame, const chronon3d::Framebuffer&) {
            return frame != chronon3d::Frame{3};
        });

    CHECK_FALSE(result.ok);
    CHECK(result.failed_frame == chronon3d::Frame{3});
    CHECK(result.frames_encoded == 3);
    CHECK(job.telemetry().pipeline_in_flight == 0);
    CHECK(job.render(chronon3d::Frame{0}) != nullptr);
}

TEST_CASE("PreparedRenderJob bounded pipeline preserves serial framebuffer hashes") {
    const chronon3d::Composition composition{
        chronon3d::CompositionSpec{
            .name = "prepared-render-job-pipeline-parity",
            .width = 48,
            .height = 32,
            .frame_rate = {30, 1},
            .duration = chronon3d::Frame{3}},
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder scene(ctx.resource);
            scene.rect("pipeline-rect", {
                .size = {24.0f, 16.0f},
                .color = chronon3d::Color{0.8f, 0.3f, 0.1f, 1.0f},
                .pos = {static_cast<float>(ctx.local_time().integral_frame().integral()) * 2.0f, 0.0f, 0.0f},
            });
            return scene.build();
        }};

    chronon3d::RenderEngine serial_engine;
    auto serial_job = serial_engine.prepare(composition);
    std::vector<std::uint64_t> serial_hashes;
    for (int frame = 0; frame < 3; ++frame) {
        const auto output = serial_job.render(chronon3d::Frame{frame});
        REQUIRE(output != nullptr);
        serial_hashes.push_back(chronon3d::test::framebuffer_hash(*output));
    }

    chronon3d::RenderEngine pipeline_engine;
    auto pipeline_job = pipeline_engine.prepare(composition);
    std::vector<std::uint64_t> pipeline_hashes;
    const auto result = pipeline_job.render_frames(
        chronon3d::Frame{0}, chronon3d::Frame{3},
        [&](chronon3d::Frame, const chronon3d::Framebuffer& output) {
            pipeline_hashes.push_back(
                chronon3d::test::framebuffer_hash(output));
            return true;
        });

    REQUIRE(result.ok);
    CHECK(result.frames_rendered == 3);
    CHECK(result.frames_encoded == 3);
    CHECK(pipeline_hashes == serial_hashes);
}
