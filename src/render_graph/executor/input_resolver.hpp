#pragma once

#include "execution_state.hpp"
#include <chronon3d/internal/render_graph/render_graph.hpp>

#include <atomic>
#include <memory>
#include <vector>

namespace chronon3d::graph {

void resolve_inputs(
    const RenderGraph& graph,
    GraphNodeId id,
    ExecutionState& state,
    const std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    PreResolvedNode& resolved
);

} // namespace chronon3d::graph
