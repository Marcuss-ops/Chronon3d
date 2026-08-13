#include <doctest/doctest.h>

#include <chronon3d/runtime/resource_plan.hpp>

TEST_CASE("ResourcePlanner aliases non-overlapping compatible resources") {
    chronon3d::runtime::ResourcePlanner planner;
    planner.add({"a", chronon3d::runtime::ResourceKind::Color, 32, {}, 0, 1, 16});
    planner.add({"b", chronon3d::runtime::ResourceKind::Color, 64, {}, 2, 3, 32});

    const auto plan = planner.build();
    REQUIRE(plan.slots.size() == 1);
    CHECK(plan.allocation_for(0)->physical_slot == plan.allocation_for(1)->physical_slot);
    CHECK(plan.slots[0].bytes == 64);
    CHECK(plan.slots[0].alignment == 32);
    CHECK(plan.planned_physical_bytes == 64);
    CHECK(plan.peak_live_bytes == 64);
}

TEST_CASE("ResourcePlanner never aliases overlapping or incompatible resources") {
    chronon3d::runtime::ResourcePlanner planner;
    planner.add({"a", chronon3d::runtime::ResourceKind::Color, 32, {}, 0, 2, 16});
    planner.add({"b", chronon3d::runtime::ResourceKind::Color, 32, {}, 2, 3, 16});
    planner.add({"depth", chronon3d::runtime::ResourceKind::Depth, 32, {}, 3, 4, 16});

    const auto plan = planner.build();
    REQUIRE(plan.slots.size() == 3);
    CHECK(plan.planned_physical_bytes == 96);
    CHECK(plan.peak_live_bytes == 64);
}

TEST_CASE("ResourcePlanner never aliases different lifetime domains") {
    chronon3d::runtime::ResourcePlanner planner;
    planner.add({"frame", chronon3d::runtime::ResourceKind::Color, 32,
                 chronon3d::runtime::LifetimeClass::FrameTransient, 0, 0, 16});
    planner.add({"slot", chronon3d::runtime::ResourceKind::Color, 32,
                 chronon3d::runtime::LifetimeClass::PipelineSlot, 1, 1, 16});
    planner.add({"job", chronon3d::runtime::ResourceKind::Color, 32,
                 chronon3d::runtime::LifetimeClass::JobPersistent, 0, 0, 16});

    const auto plan = planner.build();
    CHECK(plan.slots.size() == 3);
}

TEST_CASE("ResourcePlanner compatibility includes format usage and dimensions") {
    chronon3d::runtime::ResourcePlanner planner;
    using namespace chronon3d::runtime;

    ResourceRequest color_a{"color-a", ResourceKind::Color, 128, {}, 0, 0, 16};
    color_a.desc = ResourceDesc{64, 2, PixelFormat::Rgba32Float,
                                ResourceUsage::ColorAttachment, 128, 16};
    ResourceRequest color_b{"color-b", ResourceKind::Color, 128, {}, 1, 1, 16};
    color_b.desc = ResourceDesc{64, 2, PixelFormat::Rgba32Float,
                                ResourceUsage::ColorAttachment, 128, 16};
    ResourceRequest depth{"depth", ResourceKind::Depth, 128, {}, 2, 2, 16};
    depth.desc = ResourceDesc{64, 2, PixelFormat::Depth32Float,
                              ResourceUsage::DepthAttachment, 128, 16};

    planner.add(color_a);
    planner.add(color_b);
    planner.add(depth);
    const auto plan = planner.build();

    CHECK(plan.slots.size() == 2);
    CHECK(plan.allocation_for(0)->physical_slot == plan.allocation_for(1)->physical_slot);
    CHECK(plan.allocation_for(0)->physical_slot != plan.allocation_for(2)->physical_slot);
    CHECK(plan.telemetry.logical_count == 3);
    CHECK(plan.telemetry.physical_count == 2);
    CHECK(plan.telemetry.buffer_reuse_count == 1);
    CHECK(plan.telemetry.buffer_new_allocations == 2);
    CHECK(plan.telemetry.logical_bytes == 384);
    CHECK(plan.telemetry.physical_bytes == 256);
    CHECK(plan.telemetry.alias_saved_bytes == 128);
}

TEST_CASE("ResourcePlanner rejects a smaller physical slot for a larger descriptor") {
    chronon3d::runtime::ResourcePlanner planner;
    using namespace chronon3d::runtime;

    ResourceRequest small{"small", ResourceKind::Color, 64, {}, 0, 0, 16};
    small.desc = ResourceDesc{4, 4, PixelFormat::Rgba8Unorm,
                              ResourceUsage::ColorAttachment, 64, 16};
    ResourceRequest large{"large", ResourceKind::Color, 256, {}, 1, 1, 16};
    large.desc = ResourceDesc{8, 8, PixelFormat::Rgba8Unorm,
                              ResourceUsage::ColorAttachment, 256, 16};

    planner.add(small);
    planner.add(large);
    const auto plan = planner.build();

    CHECK(plan.slots.size() == 2);
    CHECK(plan.telemetry.alias_saved_bytes == 0);
}

TEST_CASE("ResourcePlanner keeps persistent resources distinct and excludes external resources") {
    using namespace chronon3d::runtime;
    ResourcePlanner planner;
    ResourceRequest persistent_a{"persistent-a", ResourceKind::Color, 64,
                                LifetimeClass::JobPersistent, 0, 0, 16};
    persistent_a.desc.lifetime = ResourceLifetime::Persistent;
    ResourceRequest persistent_b{"persistent-b", ResourceKind::Color, 64,
                                LifetimeClass::JobPersistent, 1, 1, 16};
    persistent_b.desc.lifetime = ResourceLifetime::Persistent;
    ResourceRequest external{"external", ResourceKind::Color, 64, {}, 0, 1, 16};
    external.desc.lifetime = ResourceLifetime::External;
    planner.add(persistent_a);
    planner.add(persistent_b);
    planner.add(external);

    const auto plan = planner.build();
    CHECK(plan.slots.size() == 2);
    CHECK(plan.allocation_for(0)->physical_slot != plan.allocation_for(1)->physical_slot);
    CHECK(plan.allocation_for(2)->physical_slot ==
          std::numeric_limits<std::size_t>::max());
    CHECK(plan.slots[0].offset == 0);
    CHECK(plan.slots[1].offset >= plan.slots[0].offset + plan.slots[0].bytes);
}
