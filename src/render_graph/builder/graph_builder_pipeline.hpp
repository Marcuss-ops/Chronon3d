#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>
#include "graph_builder_internal.hpp"
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d {
    class SoftwareRenderer;
}

namespace chronon3d::graph::detail {

RenderGraph build_graph(const Scene& scene, const RenderGraphContext& ctx,
                        const LayerResolutionResult& resolved);

bool is_native_3d_layer(const Layer& layer);
raster::BBox compute_layer_bbox(const LayerGraphItem& item, const RenderGraphContext& ctx, SoftwareRenderer* renderer);

} // namespace chronon3d::graph::detail

