#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/render/resolved_types.hpp>
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

} // namespace chronon3d::graph::detail
