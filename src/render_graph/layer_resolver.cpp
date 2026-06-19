// =============================================================================
// layer_resolver.cpp — Public implementation of resolve_layers()
//
// Moved from graph_builder to graph_core so that graph_nodes can call it
// without creating a graph_nodes → graph_builder cycle.
// =============================================================================

#include <chronon3d/render_graph/layer/layer_resolver.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/scene/model/layer/layer_hierarchy.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <tbb/task_group.h>
#include <unordered_map>

namespace chronon3d::graph::detail {

LayerResolutionResult resolve_layers(const Scene& scene, const RenderGraphContext& ctx) {
    chronon3d::detail::LayerHierarchyResolver resolver(scene.layers(), scene.resource());

    LayerResolutionResult result;
    tbb::task_group tg;

    tg.run([&]() {
        result.camera = resolver.resolve_camera(scene.camera_2_5d());
    });

    tg.run([&]() {
        result.layers = resolver.resolve_layers(ctx.frame.frame);
    });

    tg.wait();

    if (ctx.options.modular_coordinates) {
        // Build a map from layer name to its ResolvedLayer
        std::unordered_map<std::string_view, ResolvedLayer*> name_to_resolved;
        name_to_resolved.reserve(result.layers.size());
        for (auto& rl : result.layers) {
            if (rl.layer) {
                name_to_resolved[rl.layer->name] = &rl;
            }
        }

        const f32 half_w = ctx.frame.width * 0.5f;
        const f32 half_h = ctx.frame.height * 0.5f;

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
                if (root->layer && !root->layer->uses_2_5d_projection && (!root->layer->layout.enabled || !root->layer->layout.pin.has_value())) {
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
