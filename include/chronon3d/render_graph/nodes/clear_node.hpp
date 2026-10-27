#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class ClearNode final : public RenderGraphNode {
public:
    // The cleared output is subsequently mutated by compositing. Reusing the
    // framebuffer cached from an earlier frame therefore preserves pixels
    // from that frame in regions not overwritten by the new camera projection.
    // Re-execute the clear node for every frame.
    explicit ClearNode(bool force_clear = false, std::string cache_identity = {})
        : RenderGraphNode(frame_variant_cache(force_clear ? "group_clear" : "clear")),
          m_force_clear(force_clear),
          m_cache_identity(cache_identity.empty()
              ? (force_clear ? "group_clear" : "clear")
              : (force_clear ? "group_clear:" + cache_identity
                             : "clear:" + cache_identity)) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Output; }
    std::string_view name() const noexcept override { return "Clear"; }
    bool has_compiled_recorder() const noexcept override { return true; }


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
            .scope = m_cache_identity,
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

private:
    bool m_force_clear{false};
    std::string m_cache_identity;
};

} // namespace chronon3d::graph
