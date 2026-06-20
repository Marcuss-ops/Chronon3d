// dependency_graph.hpp — Path B dependency-graph + cycle detection.
//
// Walks the bytecode of every compiled expression in a scene to extract
// the read/write name set. Builds an adjacency list by source: from expr A's
// LOAD_VAR/STORE_VAR references to expr B (the program whose name B was
// previously written). Topologically orders via Kahn's. Cycles are reported
// with their constituent names so the caller can refuse to compile the
// scene until the user breaks them up.

#pragma once

#include <chronon3d/expressions/v2/bytecode.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chronon3d::expressions::v2 {

// ── NodeId: stable identifier for an expression in a scene graph ────
struct NodeId {
    std::string name;
    bool operator==(const NodeId& o) const noexcept { return name == o.name; }
    bool operator!=(const NodeId& o) const noexcept { return name != o.name; }
};
struct NodeIdHash {
    size_t operator()(const NodeId& n) const noexcept {
        return std::hash<std::string>{}(n.name);
    }
};

struct CycleReport {
    std::vector<NodeId> cycle_nodes;
};

// ── DependencyGraph ─────────────────────────────────────────────────
class DependencyGraph {
public:
    /// Add a single program under a node id; auto-collects the names it
    /// touches via LOAD_VAR/STORE_VAR (the others' load sides aren't seen
    /// here — they would have been recorded by previous `add_program`s).
    void add_program(const NodeId& id, const Program& program);

    /// Declare that node "writes" name. (Program walk above only sees
    /// its own reads; this call gives the graph its write-side.)
    void add_writer(const NodeId& writer, const std::string& var_name);

    /// Declare an explicit edge from->to.
    void add_edge(const NodeId& from, const NodeId& to);

    /// Topological sort: returns nodes in evaluation order. Cycles are
    /// not included in the returned vector; they're returned via the
    /// CycleReport output parameter.
    std::vector<NodeId> topological_order(CycleReport& report);

    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }
    [[nodiscard]] bool empty() const noexcept { return nodes_.empty(); }

private:
    std::unordered_map<NodeId, std::vector<std::string>, NodeIdHash> reads_;        // node -> names it loads
    std::unordered_map<NodeId, std::vector<std::string>, NodeIdHash> writes_;       // node -> names it stores
    std::unordered_map<std::string, NodeId, std::hash<std::string>> var_to_writer_; // name -> writer node
    std::unordered_set<NodeId, NodeIdHash> nodes_;
};

} // namespace chronon3d::expressions::v2
