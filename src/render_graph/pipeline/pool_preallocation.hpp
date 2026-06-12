#pragma once

// ---------------------------------------------------------------------------
// pool_preallocation.hpp
//
// Predicts the framebuffer bucket sizes a compiled graph will need for a
// given frame, then ensures those buffers are preallocated in the pool
// before graph execution begins.  Eliminates allocation stalls during
// the hot render path.
// ===========================================================================

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <vector>

namespace chronon3d::graph {

/// Analyze the compiled graph's levels + predicted bboxes to estimate how
/// many framebuffers of each bucket size will be needed concurrently.
[[nodiscard]] std::vector<cache::FramebufferPoolPreallocOptions> predict_pool_requirements(
    const CompiledFrameGraph& compiled,
    int canvas_width,
    int canvas_height);

/// Call predict_pool_requirements() then ensure_preallocated() for each
/// prediction.  Returns the total number of newly created framebuffers.
[[nodiscard]] size_t preallocate_for_frame(
    cache::FramebufferPool& pool,
    const CompiledFrameGraph& compiled,
    int width,
    int height,
    chronon3d::RenderCounters* counters = nullptr,
    bool diagnostics_enabled = false);

} // namespace chronon3d::graph
