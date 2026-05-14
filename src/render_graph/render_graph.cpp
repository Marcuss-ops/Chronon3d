#include <chronon3d/render_graph/render_graph.hpp>
#include <sstream>
#include <iomanip>

namespace chronon3d::graph {

std::string RenderGraph::to_dot() const {
    std::ostringstream out;
    out << "digraph RenderGraph {\n";
    out << "  rankdir=LR;\n";
    out << "  node [shape=box, style=filled, fillcolor=white, fontname=\"Arial\"];\n";

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const auto& node = *m_nodes[i];
        
        std::string color = "white";
        switch (node.kind()) {
            case RenderGraphNodeKind::Source:    color = "lightblue"; break;
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
