// ===========================================================================
// runtime/execution_plan_cache.cpp
//
// TICKET-009 — Implementation of `chronon3d::runtime::ExecutionPlanCache`,
// split off from `chronon3d::graph::GraphExecutor` so the executor is
// stateless.  Two pieces of code that used to live in
// `src/render_graph/executor/executor_plan.cpp` (compute_structure_signature
// and build_execution_plan) are moved here because they describe the
// cache key/value shape, not the executor's behaviour.
// ===========================================================================

#include <chronon3d/runtime/execution_plan_cache.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

#include <algorithm>
#include <stdexcept>

namespace chronon3d::runtime {

std::shared_ptr<const ExecutionPlanCache::Plan>
ExecutionPlanCache::try_acquire(
    std::uint64_t structure_hash,
    chronon3d::graph::GraphNodeId output) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_entry.valid &&
        m_entry.structure_hash == structure_hash &&
        m_entry.output == output &&
        m_entry.plan)
    {
        return m_entry.plan;
    }
    return nullptr;
}

void ExecutionPlanCache::store(
    std::uint64_t structure_hash,
    chronon3d::graph::GraphNodeId output,
    std::shared_ptr<const Plan> plan) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entry.structure_hash = structure_hash;
    m_entry.output         = output;
    m_entry.plan           = std::move(plan);
    m_entry.valid          = true;
}

void ExecutionPlanCache::invalidate() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entry = Entry{};
}

ExecutionPlanCache::Entry ExecutionPlanCache::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entry;
}

std::uint64_t ExecutionPlanCache::compute_structure_signature(
    const chronon3d::graph::RenderGraph& graph,
    chronon3d::graph::GraphNodeId output) {
    std::uint64_t sig = chronon3d::graph::hash_value(graph.size());
    for (chronon3d::graph::GraphNodeId id = 0; id < graph.size(); ++id) {
        if (!graph.has_node(id)) continue;
        sig = chronon3d::graph::hash_combine(
            sig,
            chronon3d::graph::hash_value(static_cast<int>(graph.node(id).kind())));
        const auto& inputs = graph.inputs(id);
        sig = chronon3d::graph::hash_combine(sig, chronon3d::graph::hash_value(inputs.size()));
        for (chronon3d::graph::GraphNodeId input : inputs) {
            sig = chronon3d::graph::hash_combine(sig, chronon3d::graph::hash_value(input));
        }
    }
    sig = chronon3d::graph::hash_combine(sig, chronon3d::graph::hash_value(output));
    return sig;
}

std::shared_ptr<const ExecutionPlanCache::Plan>
ExecutionPlanCache::build_execution_plan(
    chronon3d::graph::RenderGraph& graph,
    chronon3d::graph::GraphNodeId output) {
    auto plan = std::make_shared<Plan>();
    const std::size_t node_count = graph.size();
    if (node_count == 0 || output >= node_count) {
        return plan;
    }

    std::vector<char> reachable(node_count, 0);
    std::vector<chronon3d::graph::GraphNodeId> stack{output};
    while (!stack.empty()) {
        const chronon3d::graph::GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id]) {
            continue;
        }
        reachable[id] = 1;
        for (const chronon3d::graph::GraphNodeId parent : graph.inputs(id)) {
            stack.push_back(parent);
        }
    }

    std::vector<std::vector<chronon3d::graph::GraphNodeId>> children(node_count);
    std::vector<std::size_t> indegree(node_count, 0);
    plan->consumer_counts.assign(node_count, 0);

    for (chronon3d::graph::GraphNodeId child = 0; child < node_count; ++child) {
        if (!reachable[child]) {
            continue;
        }
        for (const chronon3d::graph::GraphNodeId parent : graph.inputs(child)) {
            if (!reachable[parent]) {
                continue;
            }
            children[parent].push_back(child);
            ++indegree[child];
            ++plan->consumer_counts[parent];
        }
    }

    std::vector<chronon3d::graph::GraphNodeId> current_level;
    current_level.reserve(node_count);
    for (chronon3d::graph::GraphNodeId id = 0; id < node_count; ++id) {
        if (reachable[id] && indegree[id] == 0) {
            current_level.push_back(id);
        }
    }

    std::size_t scheduled = 0;
    while (!current_level.empty()) {
        plan->levels.push_back(current_level);
        scheduled += current_level.size();

        std::vector<chronon3d::graph::GraphNodeId> next_level;
        for (const chronon3d::graph::GraphNodeId id : current_level) {
            for (const chronon3d::graph::GraphNodeId child : children[id]) {
                if (--indegree[child] == 0) {
                    next_level.push_back(child);
                }
            }
        }
        current_level.swap(next_level);
    }

    const std::size_t reachable_count = static_cast<std::size_t>(
        std::count(reachable.begin(), reachable.end(), static_cast<char>(1)));
    if (scheduled != reachable_count) {
        throw std::runtime_error(
            "ExecutionPlanCache::build_execution_plan: graph is not a DAG or contains unreachable dependency cycles");
    }

    return plan;
}

} // namespace chronon3d::runtime
