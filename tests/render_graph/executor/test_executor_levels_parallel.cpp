// tests/render_graph/executor/test_executor_levels_parallel.cpp
//
// Regression tests for the render-graph level parallel-dispatch predicate.
// Parallel execution must be enabled only when BOTH conditions hold:
//   1. the level contains more than one node, and
//   2. the scheduler can run more than one worker concurrently.

#include <doctest/doctest.h>

#include <chronon3d/runtime/frame/frame_error_slot.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include "src/render_graph/executor/executor_levels.hpp"
#include "src/render_graph/executor/executor_compiled_program.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using chronon3d::graph::should_execute_level_in_parallel;

class NoopTestRenderBackend final : public chronon3d::graph::RenderBackend {
public:
    void apply_per_pixel_dof(
        chronon3d::Framebuffer&, std::span<const float>,
        const chronon3d::DepthOfFieldSettings&, const chronon3d::LensModel&,
        const std::optional<chronon3d::raster::BBox>&) override {}
    void apply_effect_stack(
        chronon3d::Framebuffer&, const chronon3d::EffectStack&,
        const chronon3d::effects::EffectExecutionContext&) override {}
    void composite_layer(
        chronon3d::Framebuffer&, const chronon3d::Framebuffer&,
        chronon3d::BlendMode,
        const std::optional<chronon3d::raster::BBox>&,
        chronon3d::CompositeOperator) override {}
    void apply_blur(
        chronon3d::Framebuffer&, float,
        const std::optional<chronon3d::raster::BBox>&) override {}
};

TEST_CASE("should_execute_level_in_parallel: true with >1 nodes and >1 concurrency") {
    CHECK(should_execute_level_in_parallel(2, 2) == true);
    CHECK(should_execute_level_in_parallel(2, 4) == true);
    CHECK(should_execute_level_in_parallel(5, 2) == true);
    CHECK(should_execute_level_in_parallel(5, 8) == true);
}

TEST_CASE("should_execute_level_in_parallel: false with only one node") {
    CHECK(should_execute_level_in_parallel(1, 2) == false);
    CHECK(should_execute_level_in_parallel(1, 4) == false);
    CHECK(should_execute_level_in_parallel(1, 1) == false);
}

TEST_CASE("should_execute_level_in_parallel: false with concurrency == 1") {
    CHECK(should_execute_level_in_parallel(2, 1) == false);
    CHECK(should_execute_level_in_parallel(5, 1) == false);
    CHECK(should_execute_level_in_parallel(1, 1) == false);
}

TEST_CASE("should_execute_level_in_parallel: false with zero nodes") {
    CHECK(should_execute_level_in_parallel(0, 2) == false);
    CHECK(should_execute_level_in_parallel(0, 1) == false);
    CHECK(should_execute_level_in_parallel(0, 0) == false);
}

TEST_CASE("should_execute_level_in_parallel: boundary values") {
    CHECK(should_execute_level_in_parallel(2, 2) == true);
    CHECK(should_execute_level_in_parallel(2, 1) == false);
    CHECK(should_execute_level_in_parallel(1, 2) == false);
}

TEST_CASE("fully recorded compiled-program failure is published to the frame error channel") {
    using chronon3d::graph::CompiledFrameProgram;
    using chronon3d::graph::ExecutionState;
    using chronon3d::graph::RenderGraphContext;
    using chronon3d::runtime::RenderErrorSlot;

    CompiledFrameProgram program;
    program.fully_recorded = true;
    program.levels = {{0}};
    program.rebuild_operation_index(1);

    RenderGraphContext ctx;
    RenderErrorSlot error;
    ctx.frame_error = &error;
    ExecutionState state;

    CHECK_FALSE(chronon3d::graph::execute_compiled_program(
        program, ctx, state, nullptr));
    const auto published = error.load();
    REQUIRE(published);
    CHECK(published->backend_code ==
          chronon3d::graph::RenderBackendErrorCode::InvalidInput);
    CHECK(published->node_name == "compiled-executor");
    CHECK(published->message == "fully_recorded program has no render backend");
}

TEST_CASE("compiled-program missing operation is published as a frame error") {
    using chronon3d::graph::CompiledFrameProgram;
    using chronon3d::graph::ExecutionState;
    using chronon3d::graph::RenderGraphContext;
    using chronon3d::runtime::RenderErrorSlot;

    CompiledFrameProgram program;
    program.fully_recorded = true;
    program.levels = {{0}};
    program.rebuild_operation_index(1);

    RenderGraphContext ctx;
    NoopTestRenderBackend backend;
    ctx.backend = &backend;
    RenderErrorSlot error;
    ctx.frame_error = &error;
    ExecutionState state;

    CHECK_FALSE(chronon3d::graph::execute_compiled_program(
        program, ctx, state, nullptr));
    const auto published = error.load();
    REQUIRE(published);
    CHECK(published->backend_code ==
          chronon3d::graph::RenderBackendErrorCode::InvalidInput);
    CHECK(published->node_name == "compiled-executor-node-0");
    CHECK(published->message ==
          "fully_recorded program node has no compiled_execute");
}

TEST_CASE("RenderErrorSlot publishes one complete first error under concurrent writers") {
    using chronon3d::graph::NodeExecutionError;
    using chronon3d::graph::RenderBackendErrorCode;
    using chronon3d::runtime::RenderErrorSlot;

    RenderErrorSlot slot;
    constexpr std::size_t writer_count = 16;
    std::atomic<bool> start{false};
    std::vector<std::thread> writers;
    writers.reserve(writer_count);
    for (std::size_t i = 0; i < writer_count; ++i) {
        writers.emplace_back([&, i] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            slot.publish_first(NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                "worker-" + std::to_string(i),
                "failure-" + std::to_string(i)});
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& writer : writers) writer.join();

    CHECK(slot.has_error());
    const auto first = slot.load();
    REQUIRE(first);
    CHECK(first->backend_code == RenderBackendErrorCode::ExecutionFailure);
    REQUIRE(first->node_name.starts_with("worker-"));
    REQUIRE(first->message.starts_with("failure-"));
    CHECK(first->node_name.substr(7) == first->message.substr(8));

    slot.clear();
    CHECK_FALSE(slot.has_error());
    CHECK_FALSE(slot.load());
}
