#pragma once

#include "execution_state.hpp"
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>

namespace chronon3d::graph {

/// Shared helper for disk node cache feature-flag (used by multiple executor files).
bool persistent_framebuffer_cache_enabled_for_current_run();

CacheEvalResult evaluate_cache(
    const RenderGraphNode& node,
    const RenderGraphContext& ctx,
    u64 input_hash,
    bool inputs_frame_dependent,
    bool has_cacheable_inputs,
    GraphNodeId node_id,
    bool inputs_all_cache_hits
);

} // namespace chronon3d::graph
