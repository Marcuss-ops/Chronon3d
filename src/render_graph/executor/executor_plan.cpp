#include <chronon3d/render_graph/executor/graph_executor.hpp>

#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace chronon3d::graph {

uint64_t GraphExecutor::compute_structure_signature(const RenderGraph& graph, GraphNodeId output) {
    uint64_t sig = hash_value(graph.size());
    for (GraphNodeId id = 0; id < graph.size(); ++id) {
        if (!graph.has_node(id)) continue;
        sig = hash_combine(sig, hash_value(static_cast<int>(graph.node(id).kind())));
        const auto& inputs = graph.inputs(id);
        sig = hash_combine(sig, hash_value(inputs.size()));
        for (GraphNodeId input : inputs) {
            sig = hash_combine(sig, hash_value(input));
        }
    }
    sig = hash_combine(sig, hash_value(output));
    return sig;
}

std::shared_ptr<const GraphExecutor::ExecutionPlan>
GraphExecutor::build_execution_plan(RenderGraph& graph, GraphNodeId output) const {
    auto plan = std::make_shared<ExecutionPlan>();
    const size_t node_count = graph.size();
    if (node_count == 0 || output >= node_count) {
        return plan;
    }

    std::vector<char> reachable(node_count, 0);
    std::vector<GraphNodeId> stack{output};
    while (!stack.empty()) {
        GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id]) {
            continue;
        }
        reachable[id] = 1;
        for (GraphNodeId parent : graph.inputs(id)) {
            stack.push_back(parent);
        }
    }

    std::vector<std::vector<GraphNodeId>> children(node_count);
    std::vector<size_t> indegree(node_count, 0);
    plan->consumer_counts.assign(node_count, 0);

    for (GraphNodeId child = 0; child < node_count; ++child) {
        if (!reachable[child]) {
            continue;
        }
        for (GraphNodeId parent : graph.inputs(child)) {
            if (!reachable[parent]) {
                continue;
            }
            children[parent].push_back(child);
            ++indegree[child];
            ++plan->consumer_counts[parent];
        }
    }

    std::vector<GraphNodeId> current_level;
    current_level.reserve(node_count);
    for (GraphNodeId id = 0; id < node_count; ++id) {
        if (reachable[id] && indegree[id] == 0) {
            current_level.push_back(id);
        }
    }

    size_t scheduled = 0;
    while (!current_level.empty()) {
        plan->levels.push_back(current_level);
        scheduled += current_level.size();

        std::vector<GraphNodeId> next_level;
        for (GraphNodeId id : current_level) {
            for (GraphNodeId child : children[id]) {
                if (--indegree[child] == 0) {
                    next_level.push_back(child);
                }
            }
        }
        current_level.swap(next_level);
    }

    const size_t reachable_count = static_cast<size_t>(
        std::count(reachable.begin(), reachable.end(), static_cast<char>(1))
    );
    if (scheduled != reachable_count) {
        throw std::runtime_error("GraphExecutor: graph is not a DAG or contains unreachable dependency cycles");
    }

    return plan;
}

} // namespace chronon3d::graph
