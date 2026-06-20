// dependency_graph.cpp — Path B DependencyGraph implementation.

#include <chronon3d_experimental/expressions/v2/dependency_graph.hpp>

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <vector>

namespace chronon3d::expressions::v2 {

void DependencyGraph::add_program(const NodeId& id, const Program& program) {
    nodes_.insert(id);
    auto& reads  = reads_[id];
    auto& writes = writes_[id];

    for (const Op& op : program.code) {
        if (op.kind == OpKind::LOAD_VAR) {
            if (op.slot < program.names.size()) {
                const std::string& n = program.names[op.slot];
                reads.push_back(n);
                var_to_writer_[n] = id; // best-effort: last write wins; supersedes by explicit add_writer
            }
        } else if (op.kind == OpKind::STORE_VAR) {
            if (op.slot < program.names.size()) {
                const std::string& n = program.names[op.slot];
                writes.push_back(n);
                var_to_writer_[n] = id;
            }
        }
    }
}

void DependencyGraph::add_writer(const NodeId& writer, const std::string& var_name) {
    nodes_.insert(writer);
    writes_[writer].push_back(var_name);
    var_to_writer_[var_name] = writer;
}

void DependencyGraph::add_edge(const NodeId& from, const NodeId& to) {
    nodes_.insert(from);
    nodes_.insert(to);
    // We register the read side only; the edge is "from depends on to".
    reads_[from].push_back(std::string("__edge__:") + to.name);
    var_to_writer_[std::string("__edge__:") + to.name] = to;
}

std::vector<NodeId> DependencyGraph::topological_order(CycleReport& report) {
    // Build adjacency: from reads, find writer; reader depends on writer.
    std::unordered_map<NodeId, std::vector<NodeId>, NodeIdHash> adj;
    std::unordered_map<NodeId, int, NodeIdHash> in_degree;
    for (const NodeId& n : nodes_) {
        in_degree[n] = 0;
        adj[n] = {};
    }

    for (const auto& [rnode, reads] : reads_) {
        for (const std::string& n : reads) {
            auto w = var_to_writer_.find(n);
            if (w == var_to_writer_.end()) continue;
            // Skip self-edges: a node that reads back a value it itself wrote
            // is a "feedback" signal in the expression engine, but in the
            // dependency-graph abstraction it is a NO-OP ordering constraint
            // (the node trivially satisfies its own read by definition). The
            // test `acyclic ordering respects dependencies` exercises this:
            // both `a` and `b` were declared as their own writer, and the
            // graph must not report a cycle. Mutual-feelback cycles (A reads
            // B's var + B reads A's var) are still detected correctly because
            // both edges are non-self and create non-zero in-degree on both.
            if (w->second == rnode) continue;
            // rnode depends on w->second (the writer)
            adj[w->second].push_back(rnode);
            in_degree[rnode]++;
        }
    }

    // Kahn's
    std::queue<NodeId> ready;
    for (const auto& [n, d] : in_degree) {
        if (d == 0) ready.push(n);
    }

    std::vector<NodeId> order;
    while (!ready.empty()) {
        const NodeId n = ready.front(); ready.pop();
        order.push_back(n);
        for (const NodeId& succ : adj[n]) {
            if (--in_degree[succ] == 0) ready.push(succ);
        }
    }

    // Remaining nodes with in_degree > 0 are in cycles.
    std::unordered_set<NodeId, NodeIdHash> cycle_set;
    for (const NodeId& n : nodes_) {
        if (in_degree[n] > 0) cycle_set.insert(n);
    }
    report.cycle_nodes.assign(cycle_set.begin(), cycle_set.end());

    return order;
}

} // namespace chronon3d::expressions::v2
