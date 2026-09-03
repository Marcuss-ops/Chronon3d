#include <doctest/doctest.h>

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/runtime/resource_plan.hpp>

#include <memory>
#include <string>
#include <string_view>

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

class ResourceAuthorityTestNode final : public RenderGraphNode {
public:
    explicit ResourceAuthorityTestNode(std::string name)
        : RenderGraphNode(no_cache("compiled-resource-authority")),
          name_(std::move(name)) {}

    [[nodiscard]] RenderGraphNodeKind kind() const noexcept override {
        return RenderGraphNodeKind::Source;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] cache::NodeCacheKey cache_key(
        const RenderGraphContext&) const override {
        return cache::NodeCacheKey{
            .scope = name_, .frame = 0, .width = 0, .height = 0};
    }

    NodeExecResult execute(
        RenderGraphContext&,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>) override {
        return OwnedFB{};
    }

private:
    std::string name_;
};

} // namespace

TEST_CASE("CompiledResourceTable is the complete compiled resource authority") {
    RenderGraph graph;
    const auto transient = graph.add_node(
        std::make_unique<ResourceAuthorityTestNode>("transient"));
    const auto output = graph.add_node(
        std::make_unique<ResourceAuthorityTestNode>("output"));
    graph.connect(transient, output);
    graph.set_output(output);

    RenderGraphContext ctx;
    ctx.frame_input.width = 640;
    ctx.frame_input.height = 360;

    FrameGraphCompileOptions options;
    options.run_optimizer = false;
    options.compute_lifetimes = true;

    FrameGraphCompiler compiler;
    const auto compiled = compiler.compile(std::move(graph), ctx, options);
    REQUIRE(compiled.valid);

    const auto& table = compiled.resource_table();
    REQUIRE(table.logical_resource_count == 2);
    REQUIRE(table.resource_for(transient) != nullptr);
    REQUIRE(table.resource_for(output) != nullptr);

    const auto& transient_resource = *table.resource_for(transient);
    const auto& output_resource = *table.resource_for(output);
    const auto expected_format = runtime::canonical_render_format();
    const auto expected_bytes = runtime::tight_surface_bytes(
        expected_format, 640, 360);

    CHECK(transient_resource.desc.width == 640);
    CHECK(transient_resource.desc.height == 360);
    CHECK(transient_resource.desc.format == expected_format);
    CHECK(transient_resource.desc.kind == runtime::ResourceKind::Color);
    CHECK(transient_resource.desc.usage == runtime::ResourceUsage::ColorAttachment);
    CHECK(transient_resource.desc.allocation_bytes() == expected_bytes);
    CHECK(transient_resource.desc.lifetime == runtime::LifetimeClass::FrameTransient);
    CHECK(transient_resource.physical.allocation_bytes == expected_bytes);
    CHECK(transient_resource.physical.plane_count == 1);
    CHECK(transient_resource.physical.aliasable);
    CHECK_FALSE(transient_resource.persistent);
    CHECK(transient_resource.physical_slot != kInvalidPhysicalAllocationId);
    REQUIRE(transient_resource.subresources.size() == 1);
    CHECK(transient_resource.subresources.front().id == ResourceSubresource::Whole);
    REQUIRE(transient_resource.transitions.size() == 1);
    CHECK(transient_resource.transitions.front().consumer == output);
    CHECK(transient_resource.transitions.front().ownership_transfer);
    CHECK(transient_resource.release_scheduled);
    CHECK(transient_resource.release_after_level == transient_resource.last_level);

    CHECK(output_resource.desc.width == 640);
    CHECK(output_resource.desc.height == 360);
    CHECK(output_resource.desc.format == expected_format);
    CHECK(output_resource.desc.allocation_bytes() == expected_bytes);
    CHECK(output_resource.desc.lifetime == runtime::LifetimeClass::JobPersistent);
    CHECK(output_resource.persistent);
    CHECK_FALSE(output_resource.physical.aliasable);
    CHECK(output_resource.physical_slot == kInvalidPhysicalAllocationId);
    CHECK_FALSE(output_resource.release_scheduled);

    REQUIRE(table.slots.size() == 1);
    CHECK(table.slots.front().capacity_bytes == expected_bytes);
    CHECK(table.slots.front().desc.width == 640);
    CHECK(table.slots.front().desc.height == 360);
    CHECK(table.slots.front().desc.format == expected_format);

    CHECK(table.logical_bytes == expected_bytes * 2);
    CHECK(table.planned_physical_bytes == expected_bytes);
    CHECK(table.peak_live_bytes == expected_bytes);
}

TEST_CASE("NV12 and P010 lower to real plane subresources") {
    CompiledResourcePlan nv12;
    nv12.desc = runtime::ResourceDesc::make(
        1920,
        1080,
        runtime::make_frame_format(runtime::PixelFormat::Nv12),
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::PipelineSlot);
    nv12.desc.kind = runtime::ResourceKind::Yuv;
    nv12.physical = lower_physical_requirements(nv12.desc, false);
    lower_subresources(nv12);
    CHECK(nv12.physical.plane_count == 2);
    REQUIRE(nv12.subresources.size() == 2);
    CHECK(nv12.subresources[0].id == ResourceSubresource::Plane0);
    CHECK(nv12.subresources[1].id == ResourceSubresource::Plane1);

    CompiledResourcePlan p010;
    p010.desc = runtime::ResourceDesc::make(
        3840,
        2160,
        runtime::make_frame_format(runtime::PixelFormat::P010),
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::PipelineSlot);
    p010.desc.kind = runtime::ResourceKind::Yuv;
    p010.physical = lower_physical_requirements(p010.desc, false);
    lower_subresources(p010);
    CHECK(p010.physical.plane_count == 2);
    REQUIRE(p010.subresources.size() == 2);
    CHECK(p010.subresources[0].plane_index == 0);
    CHECK(p010.subresources[1].plane_index == 1);
}
