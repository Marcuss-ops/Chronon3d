#pragma once

#include "execution_state.hpp"
#include "level_timings.hpp"

#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>

#include <atomic>
#include <memory_resource>
#include <vector>

namespace chronon3d {
struct RenderCounters;
namespace cache { class FramebufferPool; }

namespace graph {
struct CompiledFrameGraph;

void dispatch_level_nodes(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    ExecutionState& state,
    ExecutionScheduler& scheduler,
    const std::vector<GraphNodeId>& level,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    LevelTimings& timings,
    const CompiledFrameGraph& compiled);

} // namespace graph
} // namespace chronon3d
