#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
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

    [[nodiscard]] const RenderGraphNode& node(GraphNodeId id) const {
        // No lock needed for reads once graph is built and not being modified
        return *m_nodes[id];
    }

    [[nodiscard]] RenderGraphNode& node(GraphNodeId id) {
        return *m_nodes[id];
    }

    [[nodiscard]] const std::vector<GraphNodeId>& inputs(GraphNodeId id) const {
        return m_inputs[id];
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_nodes.size();
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
