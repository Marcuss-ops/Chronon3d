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
