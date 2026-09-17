#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// node_runner_fused_batch_detail.hpp — private executor interface for the
// fused layer-batch fast path (text batches only).
//
// P1.5: the implementation previously lived INLINE in this header inside an
// anonymous namespace, dragging TextRunNode/TransformNode/SourceNode and the
// whole text-subsystem include web into every TU that touched the executor.
// The body now lives in node_runner_fused_batch.cpp; this header is the only
// thing node_runner.cpp needs.
// ═══════════════════════════════════════════════════════════════════════════

#include "execution_state.hpp"

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

namespace chronon3d::graph::detail {

/// Execute a compiled text layer batch as one fused GPU dispatch. Image/video
/// sources use their native paths and are rejected here if an invalid/stale
/// compiled program attempts to route them through the retired batch.
void execute_fused_batch(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const CompiledLayerBatch& batch,
    GraphNodeId id,
    FramebufferPool* parent_pool,
    RenderCounters* parent_counters,
    const CompiledFrameGraph& compiled,
    std::uint64_t* local_nodes_executed = nullptr);

} // namespace chronon3d::graph::detail
