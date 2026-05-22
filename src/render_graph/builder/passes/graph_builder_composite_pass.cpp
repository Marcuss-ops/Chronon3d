#include "graph_builder_composite_pass.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/scene/layer/layer.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void append_composite_pass(RenderGraph& graph, GraphNodeId& current,
                           GraphNodeId layer_output, const Layer& layer,
                           const RenderGraphContext& ctx) {
    if (layer_output == k_invalid_node || layer_output == current) return;

    if (!ctx.dirty_rects_enabled &&
        current < graph.size() &&
        graph.node(current).kind() == RenderGraphNodeKind::Output &&
        graph.node(current).name() == "Clear" &&
        layer.blend_mode == chronon3d::BlendMode::Normal &&
        graph.node(layer_output).can_seed_full_frame(ctx)) {
        current = layer_output;
        return;
    }

    auto composite = graph.add_node(std::make_unique<CompositeNode>(
        layer.blend_mode,
        layer.cache_static ? Frame{0} : Frame{-1}
    ));
    graph.node(composite).set_frame_dependent(!layer.cache_static);
    graph.connect(current, composite);
    graph.connect(layer_output, composite);
    current = composite;
}

} // namespace chronon3d::graph::detail
