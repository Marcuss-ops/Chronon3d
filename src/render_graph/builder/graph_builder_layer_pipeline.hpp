#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>
#include "graph_builder_internal.hpp"

namespace chronon3d::graph::detail {

struct LayerPipelineBuilder {
    static GraphNodeId append_root_sources(RenderGraph& graph, const Scene& scene,
                                           const RenderGraphContext& ctx, GraphNodeId current);

    static void append_layer_pipeline(RenderGraph& graph, const LayerGraphItem& item,
                                       GraphNodeId& current, const RenderGraphContext& ctx,
                                       const Camera2_5DRuntime& cam25d);
};

} // namespace chronon3d::graph::detail
