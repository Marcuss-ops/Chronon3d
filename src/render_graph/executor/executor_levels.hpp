#pragma once

#include "execution_state.hpp"

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>

#include <atomic>
#include <memory_resource>
#include <vector>

namespace chronon3d {
    struct RenderCounters;
    namespace cache { class FramebufferPool; }
}

namespace chronon3d::graph {

/// Execute all DAG levels (PR-B: routed through the supplied scheduler).
///
/// @param scheduler  Thread-pool authority for the entire render process.
///                   The scheduler's `for_each_index()` is used for the
///                   per-level parallel_for; Sequential mode collapses to
///                   a serial loop inside the scheduler's arena(1) so
///                   nested tbb::parallel_for in nested graphs cannot
///                   escape the bounding arena.
void execute_levels(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    ExecutionState& state,
    ExecutionScheduler& scheduler,
    const std::vector<std::vector<GraphNodeId>>& levels,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    std::pmr::memory_resource* res
);

} // namespace chronon3d::graph
