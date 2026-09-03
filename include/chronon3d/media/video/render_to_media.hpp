#pragma once

#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>

namespace chronon3d::media::video {

/// Render-to-media is a lowering policy over the graph's canonical resource
/// plan, not a separate encoder synchronization model. The supplied resource
/// id must already denote a logical graph resource in CompiledResourceTable.
class RenderToMedia {
public:
    struct PlaneBinding {
        graph::ResourceSubresource subresource{graph::ResourceSubresource::Whole};
        std::uint32_t plane_index{0};
    };

    static void lower(
        graph::CompiledResourceTable& table,
        graph::GraphNodeId resource_id,
        graph::GraphNodeId encoder_consumer,
        std::uint32_t width,
        std::uint32_t height,
        runtime::FrameFormat format) {
        if (format.pixel != runtime::PixelFormat::Nv12 &&
            format.pixel != runtime::PixelFormat::P010) {
            throw std::invalid_argument(
                "RenderToMedia requires NV12 or P010 graph resources");
        }

        auto* plan = table.resource_for(resource_id);
        if (!plan) {
            throw std::invalid_argument(
                "RenderToMedia resource id is not a compiled graph resource");
        }

        plan->desc = runtime::ResourceDesc::make(
            width,
            height,
            format,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::PipelineSlot);
        plan->desc.kind = runtime::ResourceKind::Yuv;
        plan->persistent = false;
        plan->async_use = true;
        plan->physical = graph::lower_physical_requirements(plan->desc, false);
        graph::lower_subresources(*plan);

        if (plan->physical.plane_count != 2 || plan->subresources.size() != 2) {
            throw std::logic_error(
                "RenderToMedia biplanar format did not lower to two subresources");
        }

        // Plane synchronization is represented by normal resource transitions.
        // The encoder consumes these edges directly; no media-only fence table
        // or ownership-transfer vector is constructed.
        plan->transitions.clear();
        plan->transitions.push_back(graph::CompiledResourceTransition{
            encoder_consumer,
            graph::ResourceSubresource::Plane0,
            true});
        plan->transitions.push_back(graph::CompiledResourceTransition{
            encoder_consumer,
            graph::ResourceSubresource::Plane1,
            true});
    }

    [[nodiscard]] static std::array<PlaneBinding, 2> encoder_planes(
        const graph::CompiledResourceTable& table,
        graph::GraphNodeId resource_id) {
        const auto* plan = table.resource_for(resource_id);
        if (!plan || plan->subresources.size() != 2 ||
            plan->physical.plane_count != 2) {
            throw std::invalid_argument(
                "RenderToMedia encoder binding requires a biplanar graph resource");
        }
        return {
            PlaneBinding{plan->subresources[0].id, plan->subresources[0].plane_index},
            PlaneBinding{plan->subresources[1].id, plan->subresources[1].plane_index},
        };
    }
};

} // namespace chronon3d::media::video
