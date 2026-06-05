#pragma once

#include "execution_state.hpp"

#include <chronon3d/render_graph/render_graph.hpp>

#include <atomic>
#include <memory_resource>
#include <vector>

namespace chronon3d {
    struct RenderCounters;
    namespace cache { class FramebufferPool; }
}

namespace chronon3d::graph {

void execute_levels(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    ExecutionState& state,
    const std::vector<std::vector<GraphNodeId>>& levels,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    std::pmr::memory_resource* res
);

} // namespace chronon3d::graph
