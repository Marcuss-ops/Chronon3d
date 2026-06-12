#pragma once

// ---------------------------------------------------------------------------
// refresh/effect_stack.hpp
//
// Refreshes EffectStackNode payloads when reusing a cached compiled graph.
// Updates animated effect parameters (e.g. blur radius) from the current
// frame's evaluated layer state.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <unordered_map>
#include <string>

namespace chronon3d::graph::detail {

/// Refresh an EffectStackNode with current frame data.
/// Currently handles animated blur radius updates.
void refresh_effect_stack_node(
    EffectStackNode& node,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
    RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail
