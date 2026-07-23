#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/scene/model/core/clip_transition.hpp>
#include <span>

namespace chronon3d::graph {

class ClipTransitionNode final : public RenderGraphNode {
public:
    ClipTransitionNode(std::string name, ClipTransitionSpec spec, Frame from, Frame duration);

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::ClipTransition; }
    std::string_view name() const noexcept override { return m_name; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override;

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) const override;

    NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

private:
    std::string m_name;
    ClipTransitionSpec m_spec;
    Frame m_from{0};
    Frame m_duration{0};

    [[nodiscard]] float compute_progress(const RenderGraphContext& ctx) const;
};

} // namespace chronon3d::graph
