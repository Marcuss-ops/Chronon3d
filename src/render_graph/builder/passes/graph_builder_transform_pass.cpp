#include "graph_builder_transform_pass.hpp"
#include "../graph_builder_coordinates.hpp"

#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <memory>

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
    } else if (should_use_centered_rendering(item, ctx)) {
        transform_node = std::make_unique<TransformNode>(calculate_centered_transform(item.transform, ctx));
    } else {
        transform_node = std::make_unique<TransformNode>(item.transform);
    }

    auto transform = graph.add_node(std::move(transform_node));
    graph.connect(layer_output, transform);
    layer_output = transform;
}

} // namespace chronon3d::graph::detail
