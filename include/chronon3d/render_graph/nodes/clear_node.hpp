#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class ClearNode final : public RenderGraphNode {
public:
    ClearNode() : RenderGraphNode(static_memory_cache("clear")) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Output; }
    std::string_view name() const noexcept override { return "Clear"; }


    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override {
        if (ctx.node_exec.clip_rect) {
            return *ctx.node_exec.clip_rect;
        }
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "clear",
            .frame = 0,
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height
        };
    }

    NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override;
};

} // namespace chronon3d::graph
