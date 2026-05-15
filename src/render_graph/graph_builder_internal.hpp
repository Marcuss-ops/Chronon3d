#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/scene/layer/layer_hierarchy.hpp>

namespace chronon3d::graph::detail {

struct LayerResolutionResult {
    ResolvedCamera camera;
    std::pmr::vector<ResolvedLayer> layers;
};

LayerResolutionResult resolve_layers(const Scene& scene, const RenderGraphContext& ctx);

GraphNodeId build_layer_source(RenderGraph& graph, const Layer& layer,
                               const RenderGraphContext& ctx);

void append_layer_pipeline(RenderGraph& graph, const LayerGraphItem& item, GraphNodeId& current,
                           const RenderGraphContext& ctx, const Camera2_5D& cam25d);

} // namespace chronon3d::graph::detail
