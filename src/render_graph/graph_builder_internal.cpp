#include "graph_builder_internal.hpp"

#include <chronon3d/scene/layer/layer_hierarchy.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

LayerResolutionResult resolve_layers(const Scene& scene, const RenderGraphContext& ctx) {
    LayerResolutionResult result;
    result.layers = std::pmr::vector<ResolvedLayer>{scene.resource()};

    const Camera2_5D& input_cam = scene.camera_2_5d();
    if (scene.hierarchy_resolved()) {
        result.camera.camera = input_cam;
        result.layers.resize(scene.layers().size());
        for (usize i = 0; i < scene.layers().size(); ++i) {
            result.layers[i].layer = &scene.layers()[i];
            result.layers[i].world_transform = scene.layers()[i].transform;
            result.layers[i].insertion_index = i;
        }
    } else {
        result.layers = resolve_layer_hierarchy(
            scene.layers(), ctx.frame, scene.resource(), &input_cam, &result.camera);
    }
    return result;
}

} // namespace chronon3d::graph::detail
