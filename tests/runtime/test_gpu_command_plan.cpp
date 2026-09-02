// tests/runtime/test_gpu_command_plan.cpp
// Phase 3 compiler-authority locks: backend-neutral ResourceState hazards.

#include <doctest/doctest.h>

#include <chronon3d/runtime/gpu_command_plan.hpp>

#include <limits>

using namespace chronon3d::runtime;

namespace {

ResourceDesc color_desc() {
    return ResourceDesc::make(
        64, 64, PixelFormat::Rgba32Float,
        ResourceUsage::Generic, LifetimeClass::FrameTransient);
}

const BarrierTransition* transition_for(const CommandPlan& plan,
                                        std::size_t pass_index,
                                        RenderSurfaceHandle surface) {
    for (const auto& transition : plan.barriers.transitions) {
        if (transition.pass_index == pass_index && transition.surface == surface) {
            return &transition;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("ResourceState exposes explicit color subresource defaults") {
    const auto state = ResourceState::compute_read();
    CHECK(state.stages == PipelineStage::ComputeShader);
    CHECK(state.access == AccessMask::ShaderRead);
    CHECK(state.layout == ResourceLayout::General);
    CHECK(state.queue == QueueClass::GraphicsCompute);
    CHECK(state.range.aspects == ResourceAspect::Color);
    CHECK(state.range.first_mip == 0);
    CHECK(state.range.mip_count == 1);
    CHECK(state.range.first_layer == 0);
    CHECK(state.range.layer_count == 1);
    CHECK(state.reads());
    CHECK_FALSE(state.writes());
}

TEST_CASE("GpuCommandPlanner resolves first-write and RAW before backend") {
    GpuCommandPlanner planner;
    planner.declare_surface(1, color_desc());
    planner.declare_surface(2, color_desc());
    planner.declare_surface(3, color_desc());

    planner.composite(CompositePass{.destination = 2, .source = 1});
    planner.composite(CompositePass{.destination = 3, .source = 2});

    const auto plan = planner.build();
    REQUIRE(plan.barriers.size() == 3);

    const auto* first_write = transition_for(plan, 0, 2);
    REQUIRE(first_write != nullptr);
    CHECK(first_write->hazard == ResourceHazard::FirstWrite);
    CHECK(first_write->before.layout == ResourceLayout::Undefined);
    CHECK(first_write->after == ResourceState::compute_write());

    const auto* raw = transition_for(plan, 1, 2);
    REQUIRE(raw != nullptr);
    CHECK(raw->hazard == ResourceHazard::ReadAfterWrite);
    CHECK(raw->before.writes());
    CHECK(raw->after.reads());
}

TEST_CASE("GpuCommandPlanner elides read to read barriers") {
    GpuCommandPlanner planner;
    planner.declare_surface(1, color_desc());
    planner.declare_surface(2, color_desc());
    planner.declare_surface(3, color_desc());

    planner.composite(CompositePass{.destination = 2, .source = 1});
    planner.composite(CompositePass{.destination = 3, .source = 1});

    const auto plan = planner.build();
    CHECK(transition_for(plan, 0, 1) == nullptr);
    CHECK(transition_for(plan, 1, 1) == nullptr);
}

TEST_CASE("GpuCommandPlanner resolves WAR and WAW hazards") {
    SUBCASE("WAR") {
        GpuCommandPlanner planner;
        planner.declare_surface(1, color_desc());
        planner.declare_surface(2, color_desc());
        planner.declare_surface(3, color_desc());
        planner.composite(CompositePass{.destination = 2, .source = 1});
        planner.composite(CompositePass{.destination = 1, .source = 3});

        const auto plan = planner.build();
        const auto* war = transition_for(plan, 1, 1);
        REQUIRE(war != nullptr);
        CHECK(war->hazard == ResourceHazard::WriteAfterRead);
        CHECK(war->before.reads());
        CHECK(war->after.writes());
    }

    SUBCASE("WAW") {
        GpuCommandPlanner planner;
        planner.declare_surface(1, color_desc());
        planner.declare_surface(2, color_desc());
        planner.composite(CompositePass{.destination = 2, .source = 1});
        planner.composite(CompositePass{.destination = 2, .source = 1});

        const auto plan = planner.build();
        const auto* waw = transition_for(plan, 1, 2);
        REQUIRE(waw != nullptr);
        CHECK(waw->hazard == ResourceHazard::WriteAfterWrite);
        CHECK(waw->before.writes());
        CHECK(waw->after.writes());
    }
}

TEST_CASE("In-place pass keeps read and write intent in one state") {
    GpuCommandPlanner planner;
    planner.declare_surface(1, color_desc());
    planner.composite(CompositePass{.destination = 1, .source = 1});

    const auto plan = planner.build();
    const auto* transition = transition_for(plan, 0, 1);
    REQUIRE(transition != nullptr);
    CHECK(transition->after.reads());
    CHECK(transition->after.writes());
}

TEST_CASE("Physical slot reuse starts a new logical resource state") {
    GpuCommandPlanner planner;
    planner.declare_surface(1, color_desc());
    planner.declare_surface(2, color_desc());
    planner.declare_surface(3, color_desc());
    planner.declare_surface(4, color_desc());

    // Surface 2 is live only in pass 0. Surface 4 starts in pass 1, so the
    // deterministic first-fit planner may reuse 2's physical slot for 4.
    planner.composite(CompositePass{.destination = 2, .source = 1});
    planner.composite(CompositePass{.destination = 4, .source = 3});

    const auto plan = planner.build();
    const auto slot2 = [&] {
        for (const auto& allocation : plan.resources.allocations) {
            if (allocation.surface == 2) return allocation.physical_slot;
        }
        return std::numeric_limits<std::size_t>::max();
    }();
    const auto slot4 = [&] {
        for (const auto& allocation : plan.resources.allocations) {
            if (allocation.surface == 4) return allocation.physical_slot;
        }
        return std::numeric_limits<std::size_t>::max();
    }();

    REQUIRE(slot2 != std::numeric_limits<std::size_t>::max());
    REQUIRE(slot2 == slot4);
    const auto* first_write = transition_for(plan, 1, 4);
    REQUIRE(first_write != nullptr);
    CHECK(first_write->hazard == ResourceHazard::FirstWrite);
    CHECK(first_write->before.layout == ResourceLayout::Undefined);
}
