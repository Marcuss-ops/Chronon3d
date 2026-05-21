#include <chronon3d/render_graph/graph_filter.hpp>

namespace chronon3d::graph {

LayerGraphSelection find_layer_nodes(
    const RenderGraph& graph,
    const std::string& layer_id)
{
    LayerGraphSelection selection;

    for (GraphNodeId id = 0; id < static_cast<GraphNodeId>(graph.size()); ++id) {
        const auto& node = graph.node(id);
        if (node.layer_id() == layer_id) {
            selection.matching_nodes.push_back(id);
            selection.selected_output = id; // last matching node wins
        }
    }

    return selection;
}

} // namespace chronon3d::graph
