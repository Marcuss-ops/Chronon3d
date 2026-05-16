#include "graph_builder_transform_pass.hpp"

#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/scene/layer/layer.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void append_transform_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                     const LayerGraphItem& item, const RenderGraphContext& ctx) {
    const Layer& layer = *item.layer;

    const bool needs_transform = item.projected
        || layer.kind == LayerKind::Precomp
        || layer.kind == LayerKind::Video
        || item.transform.any();

    if (!needs_transform) return;

    std::unique_ptr<TransformNode> transform_node;
    if (item.projected) {
        transform_node = std::make_unique<TransformNode>(item.projection_matrix,
                                                         layer.transform.opacity);
    } else if (layer.kind == LayerKind::Normal && !ctx.modular_coordinates
               && (item.transform.position.x != 0.0f || item.transform.position.y != 0.0f)) {
        // Normal 2D layers with a non-zero position: SourceNode rendered content in
        // layer-position-centered coordinates. Convert to centered offset for TransformNode.
        Transform centered = item.transform;
        centered.position.x -= ctx.width  * 0.5f;
        centered.position.y -= ctx.height * 0.5f;
        transform_node = std::make_unique<TransformNode>(centered);
    } else {
        // Precomp, Video, modular-mode layers: use position as-is.
        transform_node = std::make_unique<TransformNode>(item.transform);
    }

    auto transform = graph.add_node(std::move(transform_node));
    graph.connect(layer_output, transform);
    layer_output = transform;
}

} // namespace chronon3d::graph::detail
