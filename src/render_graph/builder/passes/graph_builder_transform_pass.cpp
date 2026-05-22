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
    const bool is_static = layer.cache_static || item.is_static;
    const Frame cache_frame = is_static ? Frame{0} : Frame{-1};
    if (item.projected) {
        transform_node = std::make_unique<TransformNode>(item.projection_matrix,
                                                         layer.transform.opacity,
                                                         cache_frame);
    } else {
        Mat4 effective_matrix = item.world_matrix;
        if (should_use_centered_rendering(item, ctx)) {
            effective_matrix = math::translate(Vec3(-ctx.width * 0.5f, -ctx.height * 0.5f, 0.0f)) * effective_matrix;
        }
        transform_node = std::make_unique<TransformNode>(effective_matrix,
                                                         item.transform.opacity,
                                                         cache_frame);
    }

    auto transform = graph.add_node(std::move(transform_node));
    graph.node(transform).set_frame_dependent(!is_static);
    graph.connect(layer_output, transform);
    layer_output = transform;
}

} // namespace chronon3d::graph::detail
