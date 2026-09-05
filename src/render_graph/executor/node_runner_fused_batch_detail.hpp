#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// node_runner_fused_batch_detail.hpp — private executor interface for the
// fused layer-batch fast path (text batches + image batches).
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

/// Execute a compiled layer batch as one fused GPU dispatch.  Text batches
/// go through draw_text_batch (CHRONON3D_ENABLE_TEXT builds only); image
/// batches are materialized into runtime::GpuLayerBatch instances and
/// dispatched via execute_layer_batch.  Fail-closed: any unmaterializable
/// input throws rather than degrading to a CPU path.
void execute_fused_batch(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const CompiledLayerBatch& batch,
    GraphNodeId id,
    FramebufferPool* parent_pool,
    RenderCounters* parent_counters,
    const CompiledFrameGraph& compiled);

} // namespace chronon3d::graph::detail
