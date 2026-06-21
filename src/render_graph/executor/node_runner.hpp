#pragma once

// ---------------------------------------------------------------------------
// node_runner.hpp — execute_single_node signature.
//
// WP 4.3 — execute_single_node now receives a `const CompiledFrameGraph&`
// so it can read each compiled node's `NodeIdentity` (the
// `(compiled.graph_instance_id, compiled.nodes[id].stable_node_id)` pair)
// and write it into `ctx.node_exec.current_identity` immediately before
// `node->execute(...)` runs.  This is what lets production code paths
// (PrecompNode and downstream instruments) read a deterministic identity
// from the execution state without reaching back to the compiled graph.
// ---------------------------------------------------------------------------

#include <atomic>
#include <cstddef>
#include <memory_resource>
#include <vector>

#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>

namespace chronon3d {
    struct RenderCounters;
    namespace cache { class FramebufferPool; }
}

namespace chronon3d::graph {

struct ExecutionState;
struct PreResolvedNode;
struct CompiledFrameGraph;

void execute_single_node(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const std::pmr::vector<PreResolvedNode>& level_resolved,
    GraphNodeId id,
    size_t level_index,
    RenderCounters* parent_counters,
    cache::FramebufferPool* parent_pool,
    std::pmr::vector<std::atomic_size_t>& consumer_remaining,
    double* out_cache_ms,
    double* out_dirty_ms,
    double* out_telemetry_ms,
    double* out_execute_ms,
    double* out_predicted_bbox_ms,
    double* out_clone_context_ms,
    double* out_state_assign_ms,
    const CompiledFrameGraph& compiled
);

} // namespace chronon3d::graph
