#include <chronon3d/render_graph/render_graph.hpp>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <stdexcept>

namespace chronon3d::graph {

void RenderGraph::validate_pre_freeze() const {
    // 1. Output must be set and valid.
    if (m_output == k_invalid_node) {
        throw std::logic_error("RenderGraph::freeze: output node not set");
    }
    if (m_output >= m_nodes.size() || !m_nodes[m_output]) {
        throw std::logic_error("RenderGraph::freeze: output node is invalid or removed");
    }

    // 2. Check for self-loops and duplicate inputs.
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (!m_nodes[i]) continue;  // removed node, skip
        std::unordered_set<GraphNodeId> seen;
        for (GraphNodeId in : m_inputs[i]) {
            if (in == static_cast<GraphNodeId>(i)) {
                throw std::logic_error(
                    "RenderGraph::freeze: self-loop detected on node " + std::to_string(i));
            }
            if (in >= m_nodes.size() || !m_nodes[in]) {
                throw std::logic_error(
                    "RenderGraph::freeze: node " + std::to_string(i)
                    + " references removed/invalid input " + std::to_string(in));
            }
            if (!seen.insert(in).second) {
                throw std::logic_error(
                    "RenderGraph::freeze: duplicate input " + std::to_string(in)
                    + " on node " + std::to_string(i));
            }
        }
    }

    // 3. Check for cycles (DFS with visiting set).
    {
        enum class CycleState : u8 { Unvisited, Visiting, Visited };
        std::vector<CycleState> cyc_state(m_nodes.size(), CycleState::Unvisited);
        std::vector<GraphNodeId> cyc_stack;

        auto dfs_cycle = [&](auto& self, GraphNodeId id) -> void {
            if (cyc_state[id] == CycleState::Visiting) {
                throw std::logic_error(
                    "RenderGraph::freeze: cycle detected involving node "
                    + std::to_string(id));
            }
            if (cyc_state[id] == CycleState::Visited) return;
            cyc_state[id] = CycleState::Visiting;
            for (GraphNodeId in : m_inputs[id]) {
                if (in != k_invalid_node) self(self, in);
            }
            cyc_state[id] = CycleState::Visited;
        };

        for (size_t i = 0; i < m_nodes.size(); ++i) {
            if (m_nodes[i] && cyc_state[i] == CycleState::Unvisited) {
                dfs_cycle(dfs_cycle, static_cast<GraphNodeId>(i));
            }
        }
    }

    // 4. Check for unreachable nodes (no path from output).
    // BFS/DFS backward from output to find all reachable producers.
    std::unordered_set<GraphNodeId> reachable;
    std::vector<GraphNodeId> stack;
    stack.push_back(m_output);
    while (!stack.empty()) {
        GraphNodeId id = stack.back();
        stack.pop_back();
        if (!reachable.insert(id).second) continue;
        for (GraphNodeId in : m_inputs[id]) {
            if (in != k_invalid_node && !reachable.contains(in)) {
                stack.push_back(in);
            }
        }
    }
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i] && !reachable.contains(static_cast<GraphNodeId>(i))) {
            throw std::logic_error(
                "RenderGraph::freeze: unreachable node " + std::to_string(i)
                + " (" + m_nodes[i]->name() + ")");
        }
    }
}

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
