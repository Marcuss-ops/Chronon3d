// tests/runtime/test_gpu_command_plan.cpp
// Compiler-authority locks: canonical ResourceTransition hazards.

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

ResourceDesc yuv_desc() {
    auto desc = color_desc();
    desc.kind = ResourceKind::Yuv;
    desc.format.pixel = PixelFormat::Nv12;
    return desc;
}

RenderSurfaceHandle surface_for(const CommandPlan& plan,
                                const ResourceTransition& transition) {
    if (transition.resource >= plan.resources.requests.size()) {
        return kInvalidRenderSurfaceHandle;
    }
    return plan.resources.requests[transition.resource].surface;
}

const ResourceTransition* transition_for(const CommandPlan& plan,
                                         std::size_t pass_index,
                                         RenderSurfaceHandle surface) {
    for (const auto& transition : plan.transitions) {
        if (transition.consumer_pass == pass_index &&
            surface_for(plan, transition) == surface) {
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

TEST_CASE("GpuCommandPlanner emits canonical first-write and RAW transitions") {
    GpuCommandPlanner planner;
    planner.declare_surface(1, color_desc());
    planner.declare_surface(2, color_desc());
    planner.declare_surface(3, color_desc());

    planner.composite(CompositePass{.destination = 2, .source = 1});
    planner.composite(CompositePass{.destination = 3, .source = 2});

    const auto plan = planner.build();
    REQUIRE(plan.transitions.size() == 3);

    const auto* first_write = transition_for(plan, 0, 2);
    REQUIRE(first_write != nullptr);
    CHECK(first_write->before.layout == ResourceLayout::Undefined);
    CHECK(first_write->after.writes());
    CHECK_FALSE(first_write->after.reads());
    CHECK(first_write->after.layout == ResourceLayout::General);
    CHECK(first_write->consumer_pass == 0);
    CHECK_FALSE(first_write->queue_ownership_transfer);

    const auto* raw = transition_for(plan, 1, 2);
    REQUIRE(raw != nullptr);
    CHECK(raw->before.writes());
    CHECK(raw->after.reads());
    CHECK_FALSE(raw->after.writes());
    CHECK(raw->producer_pass == 0);
    CHECK(raw->consumer_pass == 1);
}

TEST_CASE("GpuCommandPlanner elides read to read transitions") {
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

TEST_CASE("GpuCommandPlanner resolves WAR and WAW through state transitions") {
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
        CHECK(war->before.reads());
        CHECK_FALSE(war->before.writes());
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
        CHECK(waw->before.writes());
        CHECK(waw->after.writes());
    }
}

TEST_CASE("In-place pass keeps read and write intent in one canonical state") {
    GpuCommandPlanner planner;
    planner.declare_surface(1, color_desc());
    planner.composite(CompositePass{.destination = 1, .source = 1});

    const auto plan = planner.build();
    const auto* transition = transition_for(plan, 0, 1);
    REQUIRE(transition != nullptr);
    CHECK(transition->after.reads());
    CHECK(transition->after.writes());
}

TEST_CASE("Physical slot reuse emits an explicit alias boundary") {
    GpuCommandPlanner planner;
    planner.declare_surface(1, color_desc());
    planner.declare_surface(2, color_desc());
    planner.declare_surface(3, color_desc());
    planner.declare_surface(4, color_desc());

    planner.composite(CompositePass{.destination = 2, .source = 1});
    planner.composite(CompositePass{.destination = 4, .source = 3});

    const auto plan = planner.build();
    const auto slot_for = [&](RenderSurfaceHandle surface) {
        for (const auto& allocation : plan.resources.allocations) {
            if (allocation.surface == surface) return allocation.physical_slot;
        }
        return std::numeric_limits<std::size_t>::max();
    };

    const auto slot2 = slot_for(2);
    const auto slot4 = slot_for(4);
    REQUIRE(slot2 != std::numeric_limits<std::size_t>::max());
    REQUIRE(slot2 == slot4);

    const auto* first_write = transition_for(plan, 1, 4);
    REQUIRE(first_write != nullptr);
    CHECK(first_write->before.layout == ResourceLayout::Undefined);
    CHECK(first_write->after.writes());
    CHECK(first_write->alias_boundary);
}

TEST_CASE("ResourceStateTracker keeps media planes as independent states") {
    ResourceStateTracker tracker;
    ResourceStateResolver resolver;

    ResourceUse plane0_write{
        7, UsageIntent::StorageWrite, image_range(ResourceAspect::Plane0), false};
    ResourceUse plane1_write{
        7, UsageIntent::StorageWrite, image_range(ResourceAspect::Plane1), false};
    ResourceUse plane0_read{
        7, UsageIntent::StorageRead, image_range(ResourceAspect::Plane0), false};

    auto write0 = resolver.resolve(UsageIntent::StorageWrite, ResourceKind::Yuv);
    write0.range.aspects = ResourceAspect::Plane0;
    auto write1 = resolver.resolve(UsageIntent::StorageWrite, ResourceKind::Yuv);
    write1.range.aspects = ResourceAspect::Plane1;
    auto read0 = resolver.resolve(UsageIntent::StorageRead, ResourceKind::Yuv);
    read0.range.aspects = ResourceAspect::Plane0;

    tracker.apply_use(0, plane0_write, write0);
    tracker.apply_use(0, plane1_write, write1);
    tracker.apply_use(1, plane0_read, read0);

    REQUIRE(tracker.transitions().size() == 3);
    const auto& raw = tracker.transitions().back();
    REQUIRE(std::holds_alternative<SubresourceRange>(raw.range));
    CHECK(std::get<SubresourceRange>(raw.range).aspects == ResourceAspect::Plane0);
    CHECK(raw.before.range.aspects == ResourceAspect::Plane0);
    CHECK(raw.after.range.aspects == ResourceAspect::Plane0);
    CHECK(raw.before.writes());
    CHECK(raw.after.reads());
}

TEST_CASE("GpuCommandPlanner emits canonical Plane0 and Plane1 transitions") {
    GpuCommandPlanner planner;
    planner.declare_surface(1, yuv_desc());
    planner.declare_surface(2, yuv_desc());
    planner.yuv_overlay(YuvOverlayPass{
        .destination = 2,
        .source = 1,
        .format = PixelFormat::Nv12});

    const auto plan = planner.build();
    std::size_t plane0 = 0;
    std::size_t plane1 = 0;
    for (const auto& transition : plan.transitions) {
        if (surface_for(plan, transition) != 2 ||
            !std::holds_alternative<SubresourceRange>(transition.range)) {
            continue;
        }
        const auto aspect = std::get<SubresourceRange>(transition.range).aspects;
        if (aspect == ResourceAspect::Plane0) ++plane0;
        if (aspect == ResourceAspect::Plane1) ++plane1;
    }
    CHECK(plane0 == 1);
    CHECK(plane1 == 1);
}

TEST_CASE("External surface boundary states produce release and acquire ownership") {
    GpuCommandPlanner planner;
    auto external_desc = color_desc();
    external_desc.lifetime = LifetimeClass::External;

    ResourceState imported;
    imported.stages = PipelineStage::Host;
    imported.access = AccessMask::HostWrite;
    imported.layout = ResourceLayout::External;
    imported.queue = QueueClass::External;

    ResourceState released;
    released.stages = PipelineStage::Host;
    released.access = AccessMask::HostRead;
    released.layout = ResourceLayout::External;
    released.queue = QueueClass::External;

    planner.declare_surface(1, external_desc, imported, released);
    planner.declare_surface(2, color_desc());
    planner.composite(CompositePass{.destination = 2, .source = 1});

    const auto plan = planner.build();
    const auto* acquire = transition_for(plan, 0, 1);
    REQUIRE(acquire != nullptr);
    CHECK(acquire->before.queue == QueueClass::External);
    CHECK(acquire->after.queue == QueueClass::Compute);
    CHECK(acquire->queue_ownership_transfer);

    const auto* release = transition_for(plan, plan.pass_count(), 1);
    REQUIRE(release != nullptr);
    CHECK(release->before.queue == QueueClass::Compute);
    CHECK(release->after.queue == QueueClass::External);
    CHECK(release->queue_ownership_transfer);
}

TEST_CASE("CommandPlan stores only canonical ResourceTransition sync data") {
    GpuCommandPlanner planner;
    planner.declare_surface(10, color_desc());
    planner.composite(CompositePass{.destination = 10, .source = 10});

    const auto plan = planner.build();
    REQUIRE(plan.transitions.size() == 1);
    const auto& transition = plan.transitions.front();
    REQUIRE(transition.resource < plan.resources.requests.size());
    CHECK(plan.resources.requests[transition.resource].surface == 10);
    CHECK(std::holds_alternative<SubresourceRange>(transition.range));
    CHECK(transition.after.queue == QueueClass::Compute);
}
