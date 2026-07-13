#pragma once

#include "execution_state.hpp"
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/cache/node_cache.hpp>

namespace chronon3d::graph {

CacheEvalResult evaluate_cache(
    const RenderGraphNode& node,
    const RenderGraphContext& ctx,
    u64 input_hash,
    bool inputs_frame_dependent,
    bool has_cacheable_inputs,
    GraphNodeId node_id
);

} // namespace chronon3d::graph
