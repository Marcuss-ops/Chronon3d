#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <memory>
#include <vector>

namespace chronon3d::graph {

struct RenderGraphEdge {
    GraphNodeId from;
    GraphNodeId to;
};

class RenderGraph {
public:
    GraphNodeId add_node(std::unique_ptr<RenderGraphNode> node) {
        GraphNodeId id = static_cast<GraphNodeId>(m_nodes.size());
        m_nodes.push_back(std::move(node));
        m_inputs.emplace_back();
        return id;
    }

    void connect(GraphNodeId from, GraphNodeId to) {
        m_inputs[to].push_back(from);
    }

    [[nodiscard]] const RenderGraphNode& node(GraphNodeId id) const {
        return *m_nodes[id];
    }

    [[nodiscard]] RenderGraphNode& node(GraphNodeId id) {
        return *m_nodes[id];
    }

    [[nodiscard]] const std::vector<GraphNodeId>& inputs(GraphNodeId id) const {
        return m_inputs[id];
    }

    [[nodiscard]] size_t size() const {
        return m_nodes.size();
    }

private:
    std::vector<std::unique_ptr<RenderGraphNode>> m_nodes;
    std::vector<std::vector<GraphNodeId>> m_inputs;
};

} // namespace chronon3d::graph
