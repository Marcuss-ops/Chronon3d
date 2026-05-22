#pragma once

#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "graph_builder_coordinates.hpp"

namespace chronon3d::graph::detail {

bool is_native_3d_layer(const Layer& layer);
raster::BBox compute_layer_bbox(const LayerGraphItem& item, const RenderGraphContext& ctx, SoftwareRenderer* renderer);

} // namespace chronon3d::graph::detail
