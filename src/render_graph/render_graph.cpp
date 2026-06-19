#include <chronon3d/render_graph/render_graph.hpp>
#include <sstream>
#include <iomanip>

namespace chronon3d::graph {

void RenderGraph::compact() {
    // Build old→new index mapping.  Nodes that were removed (null unique_ptr)
    // get the sentinel k_invalid_node.  Others get a contiguous new index.
    std::vector<GraphNodeId> old_to_new(m_nodes.size(), k_invalid_node);
    std::vector<std::unique_ptr<RenderGraphNode>> new_nodes;
    new_nodes.reserve(m_nodes.size());

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i]) {
            old_to_new[i] = static_cast<GraphNodeId>(new_nodes.size());
            new_nodes.push_back(std::move(m_nodes[i]));
        }
    }

    // Remap input vectors
    std::vector<std::vector<GraphNodeId>> new_inputs(new_nodes.size());
    for (size_t i = 0; i < m_inputs.size(); ++i) {
        GraphNodeId new_consumer = old_to_new[i];
        if (new_consumer == k_invalid_node) continue;  // removed node
        for (GraphNodeId old_producer : m_inputs[i]) {
            GraphNodeId new_producer = old_to_new[old_producer];
            if (new_producer != k_invalid_node) {
                new_inputs[new_consumer].push_back(new_producer);
            }
        }
    }

    // Remap output
    if (m_output != k_invalid_node && m_output < old_to_new.size()) {
        m_output = old_to_new[m_output];
    }

    m_nodes  = std::move(new_nodes);
    m_inputs = std::move(new_inputs);
}

std::string RenderGraph::to_dot() const {
    std::ostringstream out;
    out << "digraph RenderGraph {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box, style=filled, fillcolor=white, fontname=\"Arial\"];\n";

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (!m_nodes[i]) continue;
        const auto& node = *m_nodes[i];

        std::string color = "white";
        switch (node.kind()) {
            case RenderGraphNodeKind::Source:    color = "lightblue"; break;
            case RenderGraphNodeKind::TextRun:   color = "lightcyan"; break;
            case RenderGraphNodeKind::Transform: color = "lightgrey"; break;
            case RenderGraphNodeKind::Effect:    color = "lightyellow"; break;
            case RenderGraphNodeKind::Composite: color = "lightgreen"; break;
            case RenderGraphNodeKind::Output:    color = "orange"; break;
            case RenderGraphNodeKind::Video:     color = "purple"; break;
            default: break;
        }

        out << "  n" << i << " [label=\""
            << node.name() << "\""
            << ", fillcolor=\"" << color << "\""
            << "];\n";
    }

    for (size_t i = 0; i < m_inputs.size(); ++i) {
        for (GraphNodeId input : m_inputs[i]) {
            out << "  n" << input << " -> n" << i << ";\n";
        }
    }

    out << "}\n";
    return out.str();
}

} // namespace chronon3d::graph
