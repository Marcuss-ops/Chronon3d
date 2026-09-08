#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/compiler/program_sampler.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/runtime/context/runtime_context.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include <doctest/doctest.h>

#include <type_traits>
#include <utility>
#include <filesystem>

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
    CHECK(program.metadata().job_id == "compiler-boundary");
    CHECK(program.metadata().width == 1920);
    CHECK(program.metadata().height == 1080);
    CHECK(program.metadata().output.path.empty());
    CHECK(program.metadata().asset_manifest.assets().empty());
    CHECK(program.metadata().duration_frames == 1);
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
    plan.output.path = "compiler-boundary.mp4";
    plan.output.rate_control = chronon3d::render_plan::RateControlMode::Bitrate;
    plan.output.bitrate = 4'000'000;

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
    CHECK(program.metadata().output.path == "compiler-boundary.mp4");
    CHECK(program.metadata().output.bitrate == 4'000'000);
    CHECK(program.nodes().size() == 1);
    CHECK(program.nodes().front().kind == chronon3d::compiler::NodeKind::Color);
    CHECK(program.animations().size() == 1);
    CHECK(program.timelines().size() == 1);
    CHECK(program.animation_tracks().empty());
    CHECK(program.curve_keyframes().empty());
    CHECK(program.passes().size() == 1);
    CHECK(program.passes().front().node_count == 1);
    CHECK(program.metadata().render_budget.max_width == plan.budget.max_width);
    CHECK(program.fingerprint_data().digest != 0);
    CHECK(program.fingerprint_data().content_digest.hex()
          == prepared.fingerprint.content_digest.hex());
    CHECK(program.fingerprint_data().request_digest.hex()
          == prepared.fingerprint.request_digest.hex());

    chronon3d::assets::AssetResolver repeat_resolver;
    auto repeat = chronon3d::compiler::compile(plan, repeat_resolver);
    REQUIRE(repeat);
    CHECK(program.fingerprint() == repeat.value().fingerprint());
}

TEST_CASE("CompiledProgram owns typed timeline, animation and text lowering") {
    auto plan = test_plan();
    plan.layers.front().animation->tracks.push_back({
        "opacity", {{chronon3d::Frame{0}, {0.25f}}, {chronon3d::Frame{12}, {1.0f}}}, "linear"});
    chronon3d::render_plan::LayerPlan text;
    text.id = "title";
    text.type = chronon3d::render_plan::LayerType::Text;
    text.text = "Chronon";
    text.font = "tests/fixtures/Inter-Bold.ttf";
    text.style = chronon3d::render_plan::LayerStylePlan{
        .font = text.font, .font_size = 48.0f, .fill = "#ffffff"};
    plan.layers.push_back(std::move(text));

    chronon3d::assets::AssetResolver resolver;
    resolver.mount(std::filesystem::current_path());
    auto result = chronon3d::compiler::compile(plan, resolver);
    REQUIRE(result);
    const auto program = std::move(result).value();

    REQUIRE(program.timelines().size() == 2);
    CHECK(program.timelines()[1].duration_frames == plan.canvas.duration.integral());
    REQUIRE(program.animation_tracks().size() == 1);
    CHECK(program.animation_tracks().front().property == "opacity");
    CHECK(program.curve_keyframes().size() == 2);
    REQUIRE(program.texts().size() == 1);
    CHECK(program.text_values()[program.texts().front().text_index] == "Chronon");
}

TEST_CASE("TypedProgramSampler samples the compiler IR without the legacy bridge") {
    auto plan = test_plan();
    plan.layers.front().position = {10.0f, 20.0f, 0.0f};
    plan.layers.front().animation->tracks.push_back({
        "opacity", {{chronon3d::Frame{0}, {0.25f}}, {chronon3d::Frame{12}, {1.0f}}}, "linear"});
    chronon3d::assets::AssetResolver resolver;
    auto result = chronon3d::compiler::compile(plan, resolver);
    REQUIRE(result);
    const auto program = std::move(result).value();

    const auto frame = chronon3d::compiler::TypedProgramSampler{}.sample(
        program, chronon3d::Frame{6});
    REQUIRE(frame.layers.size() == 1);
    CHECK(frame.layers.front().active);
    CHECK(frame.layers.front().opacity == doctest::Approx(0.625f));
    CHECK(frame.layers.front().matrix[3][0] == doctest::Approx(10.0f));
    CHECK(frame.layers.front().matrix[3][1] == doctest::Approx(20.0f));

    const auto context = chronon3d::make_frame_context({
        .duration = plan.canvas.duration,
        .width = 1920,
        .height = 1080,
        .resource = std::pmr::get_default_resource(),
    });
    const auto scene = chronon3d::compiler::ProgramSampler{}.sample_scene(
        program, context, chronon3d::Frame{6});
    CHECK(scene.layers().size() == 1);
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
        auto* slot = job.slots().try_acquire(
            chronon3d::runtime::FrameSlotState::Evaluating);
        REQUIRE(slot != nullptr);
        auto& payload = job.slots().payload(slot->slot_id);
        chronon3d::runtime::FrameExecution frame(
            job, chronon3d::Frame{0}, time, *slot, payload);
        CHECK(frame.job().job_id() == 42);
        CHECK(frame.payload() == &payload);
        CHECK(frame.arena() == &payload.arena);
        CHECK(&frame.parameters() == &payload.parameters);
        CHECK(job.counters().frames_started == 1);
        CHECK_FALSE(frame.cancelled());
        frame.complete();
        CHECK(job.counters().frames_completed == 1);
        CHECK(job.counters().frames_failed == 0);
        job.slots().release(*slot);
    }

    job.cancellation().cancel();
    auto* cancelled_slot = job.slots().try_acquire(
        chronon3d::runtime::FrameSlotState::Evaluating);
    REQUIRE(cancelled_slot != nullptr);
    auto& cancelled_payload = job.slots().payload(cancelled_slot->slot_id);
    chronon3d::runtime::FrameExecution cancelled(
        job, chronon3d::Frame{1}, time, *cancelled_slot, cancelled_payload);
    CHECK(cancelled.cancelled());
    cancelled.fail();
    CHECK(cancelled.counters().errors == 1);
    cancelled.complete();
    CHECK(job.counters().frames_failed == 1);
    job.slots().release(*cancelled_slot);
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
