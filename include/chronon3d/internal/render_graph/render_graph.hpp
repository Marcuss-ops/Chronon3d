#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <atomic>

namespace chronon3d::graph {

struct RenderGraphEdge {
    GraphNodeId from;
    GraphNodeId to;
};

static constexpr GraphNodeId k_invalid_node = static_cast<GraphNodeId>(-1);

struct BuilderContext {
    std::string layer_id;
    RenderGraphNode::OpacityEvaluator opacity_evaluator;
};

// ── RenderGraph lifecycle ─────────────────────────────────────────────────
//
//   Building  – single-threaded mutation (add_node, connect, …)
//   Frozen    – validated, compacted, ready for concurrent reads
//
// After freeze() is called, the graph becomes immutable and all read
// methods are safe for concurrent access without internal locking.
// Mutating methods assert-fail if called after freeze().
enum class GraphPhase : u8 {
    Building,
    Frozen,
};

class RenderGraph {
public:
    RenderGraph() = default;

    RenderGraph(RenderGraph&& other) noexcept
        : m_phase(other.m_phase)
        , m_nodes(std::move(other.m_nodes))
        , m_inputs(std::move(other.m_inputs))
        , m_output(other.m_output)
    {
        other.m_output = k_invalid_node;
    }

    RenderGraph& operator=(RenderGraph&& other) noexcept {
        if (this != &other) {
            m_phase  = other.m_phase;
            m_nodes  = std::move(other.m_nodes);
            m_inputs = std::move(other.m_inputs);
            m_output = other.m_output;
            other.m_output = k_invalid_node;
        }
        return *this;
    }

    // ── Mutation (Building phase only) ──────────────────────────────────

    GraphNodeId add_node(std::unique_ptr<RenderGraphNode> node,
                         const BuilderContext& ctx = {}) {
        require_building("add_node");
        if (!node) {
            throw std::invalid_argument("RenderGraph::add_node: node must not be null");
        }
        node->set_layer_id(ctx.layer_id);
        node->set_opacity_evaluator(ctx.opacity_evaluator);
        GraphNodeId id = static_cast<GraphNodeId>(m_nodes.size());
        m_nodes.push_back(std::move(node));
        m_inputs.emplace_back();
        return id;
    }

    void connect(GraphNodeId from, GraphNodeId to) {
        require_building("connect");
        validate_node_id(from);
        validate_node_id(to);
        m_inputs[to].push_back(from);
    }

    void disconnect(GraphNodeId from, GraphNodeId to) {
        require_building("disconnect");
        validate_node_id(from);
        validate_node_id(to);
        auto& vec = m_inputs[to];
        vec.erase(std::remove(vec.begin(), vec.end(), from), vec.end());
    }

    void remove_node(GraphNodeId id) {
        require_building("remove_node");
        validate_node_id(id);
        for (auto& inputs : m_inputs) {
            inputs.erase(std::remove(inputs.begin(), inputs.end(), id), inputs.end());
        }
        m_inputs[id].clear();
        m_nodes[id].reset();
    }

    void replace_input(GraphNodeId node_id, GraphNodeId old_input, GraphNodeId new_input) {
        require_building("replace_input");
        validate_node_id(node_id);
        validate_node_id(old_input);
        validate_node_id(new_input);
        auto& vec = m_inputs[node_id];
        for (auto& in : vec) {
            if (in == old_input) {
                in = new_input;
            }
        }
    }

    void set_output(GraphNodeId id) {
        require_building("set_output");
        validate_node_id(id);
        m_output = id;
    }

    /// Post-freeze output retargeting (PR-A migration helper).
    /// Unlike `set_output` (which requires the Building phase), this method
    /// may be called on a Frozen graph to switch the output node without
    /// requiring a full graph rebuild.  Used by `command_bake_layer.cpp`
    /// to re-execute the same frozen graph with a different output node
    /// once a layer has been selected.  Validates the node id (so an
    /// invalid selection produces a std::out_of_range rather than a
    /// silent miss — the bake command wants to bail loudly on bad input).
    void retarget_output(GraphNodeId id) {
        validate_node_id(id);
        m_output = id;
    }

    // ── Freeze ──────────────────────────────────────────────────────────

    /// Transition from Building → Frozen.  After this call the graph is
    /// immutable and all read methods become safe for concurrent access.
    /// Validates: output present, no duplicate inputs, no self-loops,
    /// no references to removed nodes.
    void freeze() {
        require_building("freeze");
        validate_pre_freeze();
        compact();
        m_phase = GraphPhase::Frozen;
    }

    // ── Read (safe after freeze) ────────────────────────────────────────

    [[nodiscard]] const RenderGraphNode& node(GraphNodeId id) const {
        validate_node_id(id);
        return *m_nodes[id];
    }

    [[nodiscard]] RenderGraphNode& node(GraphNodeId id) {
        require_building("node(mutable)");
        validate_node_id(id);
        return *m_nodes[id];
    }

    [[nodiscard]] bool has_node(GraphNodeId id) const {
        return id < m_nodes.size() && m_nodes[id] != nullptr;
    }

    [[nodiscard]] const std::vector<GraphNodeId>& inputs(GraphNodeId id) const {
        validate_node_id(id);
        return m_inputs[id];
    }

    [[nodiscard]] std::vector<GraphNodeId>& inputs(GraphNodeId id) {
        require_building("inputs(mutable)");
        validate_node_id(id);
        return m_inputs[id];
    }

    [[nodiscard]] size_t size() const {
        return m_nodes.size();
    }

    /// Returns the number of non-null (live) nodes.
    [[nodiscard]] size_t live_count() const {
        size_t count = 0;
        for (const auto& n : m_nodes) {
            if (n) ++count;
        }
        return count;
    }

    /// Allocate a stable per-CompositeNode unique id (per-Graph scope).
    /// Replaces the previous `static inline std::atomic<u64> s_counter` hidden
    /// singleton in composite_node.hpp (refactor: ADR-024 DELETE+DI).
    /// Returned ids are unique within this Graph instance; they may collide
    /// across different Graph instances, which is fine because the cache-key
    /// scope is already per-graph.
    [[nodiscard]] u64 next_composite_id() noexcept {
        return composite_id_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    [[nodiscard]] GraphNodeId output() const {
        if (m_output == k_invalid_node)
            throw std::logic_error("RenderGraph: output node not set");
        return m_output;
    }

    [[nodiscard]] bool has_output() const { return m_output != k_invalid_node; }

    [[nodiscard]] GraphPhase phase() const { return m_phase; }

    [[nodiscard]] std::string to_dot() const;

    // ── Validation ──────────────────────────────────────────────────────

    /// Throws std::logic_error if the graph is not in Building phase.
    void require_building(const char* operation) const {
        if (m_phase != GraphPhase::Building) {
            throw std::logic_error(
                std::string("RenderGraph::") + operation
                + " called after freeze() — graph is immutable");
        }
    }

    /// Pre-freeze validation: checks output, self-loops, duplicate inputs,
    /// unreachable nodes.
    void validate_pre_freeze() const;

    /// Validates that `id` refers to an existing, non-removed node.
    /// Throws std::out_of_range on invalid id (including the k_invalid_node
    /// sentinel, which would otherwise pass the size check via integer
    /// conversion on signed GraphNodeId types).
    void validate_node_id(GraphNodeId id) const {
        if (id == k_invalid_node || id >= m_nodes.size() || !m_nodes[id]) {
            throw std::out_of_range(
                "RenderGraph: node " + std::to_string(id) + " not found or removed");
        }
    }

private:
    /// Compact the node vector after removals, remapping all input references.
    void compact();

    GraphPhase m_phase{GraphPhase::Building};
    std::vector<std::unique_ptr<RenderGraphNode>> m_nodes;
    std::vector<std::vector<GraphNodeId>> m_inputs;
    GraphNodeId m_output{k_invalid_node};
    std::atomic<u64> composite_id_counter_{0};  // ADR-024: per-Graph CompositeNode id counter (DELETE+DI)
};

} // namespace chronon3d::graph
