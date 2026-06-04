#pragma once

#include "execution_state.hpp"
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/color.hpp>

#include <atomic>
#include <span>
#include <vector>

namespace chronon3d::graph {

/// Pre-allocate a shared transparent framebuffer used by tile pruning.
/// Must be called BEFORE the parallel level loop to avoid data races
/// when multiple pruned nodes try to access it simultaneously.
void init_shared_transparent_fb(
    ExecutionState& state,
    const RenderGraphContext& ctx,
    std::pmr::memory_resource* res
);

/// Initialize the consumer_remaining vector from an array of consumer counts
/// (one per graph node).  Each entry counts how many downstream nodes still
/// need to read that node's output before it can be released.
std::pmr::vector<std::atomic_size_t> init_consumer_remaining(
    size_t node_count,
    std::span<const size_t> consumer_counts,
    std::pmr::memory_resource* res
);

/// After executing a level, decrement consumer counts for every input
/// referenced by nodes in this level.  When a count reaches zero the
/// corresponding framebuffer is released and its tracking metadata reset.
void release_consumed_framebuffers(
    ExecutionState& state,
    RenderGraph& graph,
    std::span<const GraphNodeId> level,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining
);

} // namespace chronon3d::graph
