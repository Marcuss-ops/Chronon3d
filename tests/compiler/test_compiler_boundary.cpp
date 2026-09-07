#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/runtime/context/runtime_context.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/runtime/job/job_session.hpp>

#include <doctest/doctest.h>

#include <type_traits>
#include <utility>

TEST_CASE("compiler boundary returns an immutable CompiledProgram") {
    static_assert(!std::is_copy_constructible_v<chronon3d::compiler::CompiledProgram>);
    static_assert(!std::is_copy_assignable_v<chronon3d::compiler::CompiledProgram>);

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "compiler-boundary";
    plan.canvas.width = 1920;
    plan.canvas.height = 1080;
    plan.canvas.duration = chronon3d::Frame{1};

    chronon3d::assets::AssetResolver resolver;
    auto result = chronon3d::compiler::compile(plan, resolver);

    REQUIRE(result);
    auto program = std::move(result).value();
    CHECK(program.job_id() == "compiler-boundary");
    CHECK(program.canvas().width == 1920);
    CHECK(program.canvas().height == 1080);
    CHECK(program.compiled_composition().composition != nullptr);
    CHECK(program.metadata().job_id == "compiler-boundary");
    CHECK(program.nodes().empty());
    CHECK(program.passes().size() == 1);
    CHECK(program.fingerprint_data().algorithm_version == 1);
}

namespace {

chronon3d::render_plan::RenderPlan test_plan() {
    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "compiler-golden";
    plan.canvas.width = 1920;
    plan.canvas.height = 1080;
    plan.canvas.duration = chronon3d::Frame{24};

    chronon3d::render_plan::LayerPlan layer;
    layer.id = "animated-color";
    layer.type = chronon3d::render_plan::LayerType::Color;
    layer.animation = chronon3d::render_plan::AnimationPlan{};
    plan.layers.push_back(std::move(layer));
    return plan;
}

std::shared_ptr<const chronon3d::compiler::CompiledProgram> shared_program() {
    chronon3d::assets::AssetResolver resolver;
    auto result = chronon3d::compiler::compile(test_plan(), resolver);
    REQUIRE(result);
    return std::make_shared<const chronon3d::compiler::CompiledProgram>(
        std::move(result).value());
}

}  // namespace

TEST_CASE("CompiledProgram lowers deterministic IR without changing legacy output") {
    const auto plan = test_plan();
    chronon3d::assets::AssetResolver compiler_resolver;
    chronon3d::assets::AssetResolver legacy_resolver;

    auto compiled = chronon3d::compiler::compile(plan, compiler_resolver);
    auto legacy = chronon3d::render_plan::compile_render_plan(plan, legacy_resolver);
    REQUIRE(compiled);
    REQUIRE(legacy);

    const auto program = std::move(compiled).value();
    const auto prepared = std::move(legacy).value();
    CHECK(program.metadata().width == 1920);
    CHECK(program.metadata().height == 1080);
    CHECK(program.nodes().size() == 1);
    CHECK(program.nodes().front().kind == chronon3d::compiler::NodeKind::Color);
    CHECK(program.animations().size() == 1);
    CHECK(program.passes().size() == 1);
    CHECK(program.passes().front().node_count == 1);
    CHECK(program.compiled_composition().fingerprint == prepared.compiled_composition.fingerprint);

    chronon3d::assets::AssetResolver repeat_resolver;
    auto repeat = chronon3d::compiler::compile(plan, repeat_resolver);
    REQUIRE(repeat);
    CHECK(program.fingerprint() == repeat.value().fingerprint());
}

TEST_CASE("JobSession and FrameExecution enforce job and frame lifetimes") {
    chronon3d::runtime::RuntimeContext runtime{chronon3d::Config{}};
    auto program = shared_program();

    chronon3d::runtime::JobSessionConfig config;
    config.program = program;
    config.first_frame = chronon3d::Frame{0};
    config.end_frame = chronon3d::Frame{1};
    config.job_id = 42;
    chronon3d::runtime::JobSession job(runtime, std::move(config));

    CHECK(job.slots().capacity() == chronon3d::runtime::JobSession::kPipelineDepth);
    CHECK(job.gpu_completion().capacity() == chronon3d::runtime::JobSession::kPipelineDepth);
    CHECK(job.evaluated_queue().size() == 0);
    CHECK(job.rendered_queue().size() == 0);
    CHECK(job.frame_history().prev_frame == chronon3d::Frame{-1});
    CHECK(job.dirty_history().previous_layers.empty());

    const auto time = chronon3d::SampleTime::from_frame_int(
        chronon3d::Frame{0}, chronon3d::FrameRate{});
    {
        chronon3d::runtime::FrameSlotPayload payload;
        chronon3d::runtime::FrameExecution frame(
            job, chronon3d::Frame{0}, time, nullptr, &payload);
        CHECK(frame.job().job_id() == 42);
        CHECK(frame.payload() == &payload);
        CHECK(frame.arena() == &payload.arena);
        CHECK(job.counters().frames_started == 1);
        CHECK_FALSE(frame.cancelled());
        frame.complete();
        CHECK(job.counters().frames_completed == 1);
        CHECK(job.counters().frames_failed == 0);
    }

    job.cancellation().cancel();
    chronon3d::runtime::FrameExecution cancelled(job, chronon3d::Frame{1}, time);
    CHECK(cancelled.cancelled());
    cancelled.fail();
    CHECK(cancelled.counters().errors == 1);
    cancelled.complete();
    CHECK(job.counters().frames_failed == 1);
}

TEST_CASE("JobSession rejects invalid ownership ranges") {
    chronon3d::runtime::RuntimeContext runtime{chronon3d::Config{}};
    CHECK_THROWS_AS(
        chronon3d::runtime::JobSession(
            runtime, chronon3d::runtime::JobSessionConfig{}),
        std::invalid_argument);

    auto program = shared_program();
    chronon3d::runtime::JobSessionConfig inverted;
    inverted.program = std::move(program);
    inverted.first_frame = chronon3d::Frame{2};
    inverted.end_frame = chronon3d::Frame{1};
    CHECK_THROWS_AS(
        chronon3d::runtime::JobSession(runtime, std::move(inverted)),
        std::invalid_argument);
}
