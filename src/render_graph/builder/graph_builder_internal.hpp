#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/scene/layer/resolved_types.hpp>
#include <vector>
#include <memory_resource>

namespace chronon3d {
    class Scene;
    struct Layer;
}

namespace chronon3d::graph {
    struct RenderGraphContext;
    struct LayerGraphItem;
}

namespace chronon3d::graph::detail {

struct LayerResolutionResult {
    std::pmr::vector<chronon3d::ResolvedLayer> layers;
    chronon3d::ResolvedCamera camera;
};

LayerResolutionResult resolve_layers(const Scene& scene, const RenderGraphContext& ctx);

GraphNodeId build_layer_source(RenderGraph& graph, const Layer& layer,
                               const RenderGraphContext& ctx);

void append_layer_pipeline(RenderGraph& graph, const LayerGraphItem& item, GraphNodeId& current,
                           const RenderGraphContext& ctx, const Camera2_5DRuntime& cam25d);

} // namespace chronon3d::graph::detail
