#pragma once

#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/layer/resolved_types.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include "graph_builder_coordinates.hpp"
#include <unordered_map>
#include <string>

namespace chronon3d::graph::detail {

void compute_static_layers(
    const LayerResolutionResult& resolved,
    std::unordered_map<std::string, bool>& is_static_cache);

LayerGraphItem make_item_for_matte_source(
    const ResolvedLayer& rl,
    const RenderGraphContext& ctx,
    const Camera2_5DRuntime& cam25d,
    const std::unordered_map<std::string, bool>& is_static_cache);

} // namespace chronon3d::graph::detail
