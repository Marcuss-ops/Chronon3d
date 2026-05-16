#include "graph_builder_internal.hpp"

#include <chronon3d/scene/layer/layer_hierarchy.hpp>
#include <chronon3d/scene/scene.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

LayerResolutionResult resolve_layers(const Scene& scene, const RenderGraphContext& ctx) {
    chronon3d::detail::LayerHierarchyResolver resolver(scene.layers(), scene.resource());
    
    LayerResolutionResult result;
    result.camera = resolver.resolve_camera(scene.camera_2_5d());
    result.layers = resolver.resolve_layers(ctx.frame);
    
    return result;
}

} // namespace chronon3d::graph::detail
