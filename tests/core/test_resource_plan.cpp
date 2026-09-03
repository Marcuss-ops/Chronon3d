#include <doctest/doctest.h>

#include <chronon3d/runtime/resource_plan.hpp>

TEST_CASE("ResourcePlanner aliases non-overlapping compatible resources") {
    chronon3d::runtime::ResourcePlanner planner;
    planner.add({"a", chronon3d::runtime::ResourceKind::Color, 32, chronon3d::runtime::LifetimeClass::FrameTransient, 0, 1, 16, {}, chronon3d::runtime::kInvalidRenderSurfaceHandle});
    planner.add({"b", chronon3d::runtime::ResourceKind::Color, 64, chronon3d::runtime::LifetimeClass::FrameTransient, 2, 3, 32, {}, chronon3d::runtime::kInvalidRenderSurfaceHandle});

    const auto plan = planner.build();
    REQUIRE(plan.slots.size() == 1);
    CHECK(plan.allocation_for(0)->physical_slot == plan.allocation_for(1)->physical_slot);
    CHECK(plan.slots[0].capacity_bytes == 64);
    CHECK(plan.slots[0].desc.alignment == 32);
    CHECK(plan.planned_physical_bytes == 64);
    CHECK(plan.peak_live_bytes == 64);
}

TEST_CASE("ResourcePlanner never aliases overlapping or incompatible resources") {
    chronon3d::runtime::ResourcePlanner planner;
    planner.add({"a", chronon3d::runtime::ResourceKind::Color, 32, chronon3d::runtime::LifetimeClass::FrameTransient, 0, 2, 16, {}, chronon3d::runtime::kInvalidRenderSurfaceHandle});
    planner.add({"b", chronon3d::runtime::ResourceKind::Color, 32, chronon3d::runtime::LifetimeClass::FrameTransient, 2, 3, 16, {}, chronon3d::runtime::kInvalidRenderSurfaceHandle});
    planner.add({"depth", chronon3d::runtime::ResourceKind::Depth, 32, chronon3d::runtime::LifetimeClass::FrameTransient, 3, 4, 16, {}, chronon3d::runtime::kInvalidRenderSurfaceHandle});

    const auto plan = planner.build();
    REQUIRE(plan.slots.size() == 3);
    CHECK(plan.planned_physical_bytes == 96);
    CHECK(plan.peak_live_bytes == 64);
}

TEST_CASE("ResourcePlanner never aliases different lifetime domains") {
    chronon3d::runtime::ResourcePlanner planner;
    planner.add({"frame", chronon3d::runtime::ResourceKind::Color, 32,
                 chronon3d::runtime::LifetimeClass::FrameTransient, 0, 0, 16, {}, chronon3d::runtime::kInvalidRenderSurfaceHandle});
    planner.add({"slot", chronon3d::runtime::ResourceKind::Color, 32,
                 chronon3d::runtime::LifetimeClass::PipelineSlot, 1, 1, 16, {}, chronon3d::runtime::kInvalidRenderSurfaceHandle});
    planner.add({"job", chronon3d::runtime::ResourceKind::Color, 32,
                 chronon3d::runtime::LifetimeClass::JobPersistent, 0, 0, 16, {}, chronon3d::runtime::kInvalidRenderSurfaceHandle});

    const auto plan = planner.build();
    CHECK(plan.slots.size() == 3);
}

TEST_CASE("ResourcePlanner compatibility includes format usage and dimensions") {
    chronon3d::runtime::ResourcePlanner planner;
    using namespace chronon3d::runtime;

    ResourceRequest color_a{"color-a", ResourceKind::Color, 128, LifetimeClass::FrameTransient, 0, 0, 16, {}, kInvalidRenderSurfaceHandle};
    color_a.desc = ResourceDesc{64, 2, make_frame_format(PixelFormat::Rgba32Float),
                                ResourceUsage::ColorAttachment, LifetimeClass::FrameTransient, 128};
    ResourceRequest color_b{"color-b", ResourceKind::Color, 128, LifetimeClass::FrameTransient, 1, 1, 16, {}, kInvalidRenderSurfaceHandle};
    color_b.desc = ResourceDesc{64, 2, make_frame_format(PixelFormat::Rgba32Float),
                                ResourceUsage::ColorAttachment, LifetimeClass::FrameTransient, 128};
    ResourceRequest depth{"depth", ResourceKind::Depth, 128, LifetimeClass::FrameTransient, 2, 2, 16, {}, kInvalidRenderSurfaceHandle};
    depth.desc = ResourceDesc{64, 2, make_frame_format(PixelFormat::Depth32Float),
                              ResourceUsage::DepthAttachment, LifetimeClass::FrameTransient, 128};

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

    ResourceRequest small{"small", ResourceKind::Color, 64, LifetimeClass::FrameTransient, 0, 0, 16, {}, kInvalidRenderSurfaceHandle};
    small.desc = ResourceDesc{4, 4, make_frame_format(PixelFormat::Rgba8Unorm),
                              ResourceUsage::ColorAttachment, LifetimeClass::FrameTransient, 64};
    ResourceRequest large{"large", ResourceKind::Color, 256, LifetimeClass::FrameTransient, 1, 1, 16, {}, kInvalidRenderSurfaceHandle};
    large.desc = ResourceDesc{8, 8, make_frame_format(PixelFormat::Rgba8Unorm),
                              ResourceUsage::ColorAttachment, LifetimeClass::FrameTransient, 256};

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
                                LifetimeClass::JobPersistent, 0, 0, 16, {}, kInvalidRenderSurfaceHandle};
    persistent_a.desc.lifetime = LifetimeClass::JobPersistent;
    ResourceRequest persistent_b{"persistent-b", ResourceKind::Color, 64,
                                LifetimeClass::JobPersistent, 1, 1, 16, {}, kInvalidRenderSurfaceHandle};
    persistent_b.desc.lifetime = LifetimeClass::JobPersistent;
    ResourceRequest external{"external", ResourceKind::Color, 64, LifetimeClass::External, 0, 1, 16, {}, kInvalidRenderSurfaceHandle};
    external.desc.lifetime = LifetimeClass::External;
    planner.add(persistent_a);
    planner.add(persistent_b);
    planner.add(external);

    const auto plan = planner.build();
    CHECK(plan.slots.size() == 2);
    CHECK(plan.allocation_for(0)->physical_slot != plan.allocation_for(1)->physical_slot);
    CHECK(plan.allocation_for(2)->physical_slot ==
          std::numeric_limits<std::size_t>::max());
    CHECK(plan.slots[0].offset == 0);
    CHECK(plan.slots[1].offset >= plan.slots[0].offset + plan.slots[0].capacity_bytes);
}

TEST_CASE("ResourcePlanner makes exportable residency dedicated and non-aliasable") {
    using namespace chronon3d::runtime;
    ResourcePlanner planner;

    auto export_desc = ResourceDesc::make(
        16, 8, PixelFormat::Nv12, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 16,
        ResourceResidency{
            .domain = MemoryDomain::Vulkan,
            .device = 0,
            .exportable = true,
            .encoder_compatible = true,
        });
    export_desc.kind = ResourceKind::Yuv;
    ResourceRequest first{"export-a", export_desc, 0, 0};
    ResourceRequest second{"export-b", export_desc, 1, 1};

    planner.add(first);
    planner.add(second);
    const auto plan = planner.build();

    REQUIRE(plan.slots.size() == 2);
    CHECK(plan.allocation_for(0)->physical_slot != plan.allocation_for(1)->physical_slot);
    CHECK(plan.slots[0].dedicated);
    CHECK(plan.slots[1].dedicated);
    CHECK(plan.slots[0].desc.residency.exportable);
    CHECK(plan.slots[0].desc.residency.encoder_compatible);
    CHECK(plan.telemetry.buffer_reuse_count == 0);
    CHECK(plan.telemetry.alias_saved_bytes == 0);
}

TEST_CASE("ResourcePlanner only aliases resources with identical residency contracts") {
    using namespace chronon3d::runtime;
    ResourcePlanner planner;

    auto vulkan_desc = ResourceDesc::make(
        4, 4, PixelFormat::Rgba16Float, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 16,
        ResourceResidency{.domain = MemoryDomain::Vulkan, .device = 0});
    vulkan_desc.kind = ResourceKind::Color;
    ResourceRequest vulkan{"vulkan", vulkan_desc, 0, 0};

    auto cuda_desc = ResourceDesc::make(
        4, 4, PixelFormat::Rgba16Float, ResourceUsage::Storage,
        LifetimeClass::FrameTransient, 16,
        ResourceResidency{.domain = MemoryDomain::Cuda, .device = 0});
    cuda_desc.kind = ResourceKind::Color;
    ResourceRequest cuda{"cuda", cuda_desc, 1, 1};

    planner.add(vulkan);
    planner.add(cuda);
    const auto plan = planner.build();

    REQUIRE(plan.slots.size() == 2);
    CHECK(plan.slots[0].desc.residency.domain == MemoryDomain::Vulkan);
    CHECK(plan.slots[1].desc.residency.domain == MemoryDomain::Cuda);
}
