#pragma once

#include <chronon3d/render_graph/graph_builder.hpp>
#include "graph_builder_internal.hpp"

namespace chronon3d::graph::detail {

RenderGraph build_graph(const Scene& scene, const RenderGraphContext& ctx,
                        const LayerResolutionResult& resolved);

} // namespace chronon3d::graph::detail
