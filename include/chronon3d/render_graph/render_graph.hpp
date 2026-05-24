#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::graph {

struct RenderGraphEdge {
    GraphNodeId from;
    GraphNodeId to;
};

static constexpr GraphNodeId k_invalid_node = static_cast<GraphNodeId>(-1);

extern thread_local std::string g_current_builder_layer_id;

class RenderGraph {
public:
    RenderGraph() = default;
    
    RenderGraph(RenderGraph&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.m_mutex);
        m_nodes = std::move(other.m_nodes);
        m_inputs = std::move(other.m_inputs);
        m_output = other.m_output;
        other.m_output = k_invalid_node;
    }

    RenderGraph& operator=(RenderGraph&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(m_mutex, other.m_mutex);
            m_nodes = std::move(other.m_nodes);
            m_inputs = std::move(other.m_inputs);
            m_output = other.m_output;
            other.m_output = k_invalid_node;
        }
        return *this;
    }

    GraphNodeId add_node(std::unique_ptr<RenderGraphNode> node) {
        std::lock_guard<std::mutex> lock(m_mutex);
        GraphNodeId id = static_cast<GraphNodeId>(m_nodes.size());
        if (node) {
            node->set_layer_id(g_current_builder_layer_id);
        }
        m_nodes.push_back(std::move(node));
        m_inputs.emplace_back();
        return id;
    }

    void connect(GraphNodeId from, GraphNodeId to) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inputs[to].push_back(from);
    }

    // ── Mutation support for graph optimization ─────────────────────
    void disconnect(GraphNodeId from, GraphNodeId to) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& vec = m_inputs[to];
        vec.erase(std::remove(vec.begin(), vec.end(), from), vec.end());
    }

    void remove_node(GraphNodeId id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (id >= m_nodes.size()) return;
        // Remove this node from all consumers' input lists
        for (auto& inputs : m_inputs) {
            inputs.erase(std::remove(inputs.begin(), inputs.end(), id), inputs.end());
        }
        // Clear this node's inputs
        m_inputs[id].clear();
        // Null out the node (unique_ptr manages lifetime, we keep the slot for ID stability)
        m_nodes[id].reset();
    }

    void replace_input(GraphNodeId node_id, GraphNodeId old_input, GraphNodeId new_input) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (node_id >= m_inputs.size()) return;
        auto& vec = m_inputs[node_id];
        for (auto& in : vec) {
            if (in == old_input) {
                in = new_input;
            }
        }
    }

    [[nodiscard]] const RenderGraphNode& node(GraphNodeId id) const {
        if (id >= m_nodes.size() || !m_nodes[id]) {
            throw std::out_of_range("RenderGraph: node " + std::to_string(id) + " not found or removed");
        }
        return *m_nodes[id];
    }

    [[nodiscard]] RenderGraphNode& node(GraphNodeId id) {
        if (id >= m_nodes.size() || !m_nodes[id]) {
            throw std::out_of_range("RenderGraph: node " + std::to_string(id) + " not found or removed");
        }
        return *m_nodes[id];
    }

    [[nodiscard]] bool has_node(GraphNodeId id) const {
        return id < m_nodes.size() && m_nodes[id] != nullptr;
    }

    [[nodiscard]] const std::vector<GraphNodeId>& inputs(GraphNodeId id) const {
        return m_inputs[id];
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_nodes.size();
    }

    /// Returns the number of non-null (live) nodes after removals.
    [[nodiscard]] size_t live_count() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t count = 0;
        for (const auto& n : m_nodes) {
            if (n) ++count;
        }
        return count;
    }

    void set_output(GraphNodeId id) { 
        std::lock_guard<std::mutex> lock(m_mutex);
        m_output = id; 
    }

    [[nodiscard]] GraphNodeId output() const {
        if (m_output == k_invalid_node)
            throw std::logic_error("RenderGraph: output node not set");
        return m_output;
    }

    [[nodiscard]] bool has_output() const { return m_output != k_invalid_node; }

    [[nodiscard]] std::string to_dot() const;

private:
    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<RenderGraphNode>> m_nodes;
    std::vector<std::vector<GraphNodeId>> m_inputs;
    GraphNodeId m_output{k_invalid_node};
};

} // namespace chronon3d::graph
