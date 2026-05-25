#include "graph_builder_internal.hpp"

#include <chronon3d/scene/layer/layer_hierarchy.hpp>
#include <chronon3d/scene/scene.hpp>
#include <tbb/task_group.h>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

LayerResolutionResult resolve_layers(const Scene& scene, const RenderGraphContext& ctx) {
    chronon3d::detail::LayerHierarchyResolver resolver(scene.layers(), scene.resource());
    
    LayerResolutionResult result;
    tbb::task_group tg;
    
    tg.run([&]() {
        result.camera = resolver.resolve_camera(scene.camera_2_5d());
    });
    
    tg.run([&]() {
        result.layers = resolver.resolve_layers(ctx.frame);
    });
    
    tg.wait();
    
    if (ctx.modular_coordinates) {
        // Build a map from layer name to its ResolvedLayer
        std::unordered_map<std::string_view, ResolvedLayer*> name_to_resolved;
        name_to_resolved.reserve(result.layers.size());
        for (auto& rl : result.layers) {
            if (rl.layer) {
                name_to_resolved[rl.layer->name] = &rl;
            }
        }

        const f32 half_w = ctx.width * 0.5f;
        const f32 half_h = ctx.height * 0.5f;

        // Shift unpinned layers and propagate down the hierarchy
        for (auto& rl : result.layers) {
            if (rl.layer) {
                // Find root ancestor
                const ResolvedLayer* root = &rl;
                while (root->layer && !root->layer->parent_name.empty()) {
                    auto it = name_to_resolved.find(root->layer->parent_name);
                    if (it != name_to_resolved.end()) {
                        root = it->second;
                    } else {
                        break;
                    }
                }

                // If root ancestor is 2D and unpinned, shift this layer to screen space
                if (root->layer && !root->layer->is_3d && (!root->layer->layout.enabled || !root->layer->layout.pin.has_value())) {
                    rl.world_transform.position.x += half_w;
                    rl.world_transform.position.y += half_h;
                    rl.world_matrix = rl.world_transform.to_mat4();
                }
            }
        }
    }

    return result;
}

} // namespace chronon3d::graph::detail
