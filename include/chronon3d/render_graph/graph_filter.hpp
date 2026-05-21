#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <string>
#include <vector>

namespace chronon3d::graph {

// ---------------------------------------------------------------------------
// LayerGraphSelection
// ---------------------------------------------------------------------------
/// Result of selecting nodes belonging to a specific layer within a render graph.
struct LayerGraphSelection {
    /// All nodes whose layer_id matches the requested layer.
    std::vector<GraphNodeId> matching_nodes;

    /// The node that is considered the "output" for that layer.
    /// For V1, this is the last node in the matching_nodes list,
    /// which typically corresponds to the last node added for that layer
    /// (source → effects → transform → composite layer).
    GraphNodeId selected_output{k_invalid_node};
};

// ---------------------------------------------------------------------------
// find_layer_nodes
// ---------------------------------------------------------------------------
/// Find all nodes in the graph that belong to the given layer_id.
/// Returns the matching node IDs and selects the last one as the output.
LayerGraphSelection find_layer_nodes(
    const RenderGraph& graph,
    const std::string& layer_id);

} // namespace chronon3d::graph
