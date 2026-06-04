#pragma once

#include "execution_state.hpp"
#include <chronon3d/render_graph/render_graph.hpp>

#include <atomic>
#include <memory>
#include <vector>

namespace chronon3d::graph {

PreResolvedNode resolve_inputs(
    const RenderGraph& graph,
    GraphNodeId id,
    ExecutionState& state,
    const std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    std::pmr::memory_resource* res
);

} // namespace chronon3d::graph
