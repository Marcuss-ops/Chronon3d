#include "graph_builder_pipeline.hpp"

#include "graph_builder_camera25d_sorter.hpp"
#include "graph_builder_layer_pipeline.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/layer/layer.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

RenderGraph build_graph(const Scene& scene, const RenderGraphContext& ctx,
                        const LayerResolutionResult& resolved) {
    RenderGraph graph;
    GraphNodeId current = graph.add_node(std::make_unique<ClearNode>());
    current = LayerPipelineBuilder::append_root_sources(graph, scene, ctx, current);

    const Camera2_5D& cam25d = resolved.camera.camera;

    auto append_item = [&](const LayerGraphItem& item) {
        LayerPipelineBuilder::append_layer_pipeline(graph, item, current, ctx, cam25d);
    };

    std::vector<LayerGraphItem> current_3d_bin;
    auto flush_3d_bin = [&]() {
        if (current_3d_bin.empty()) return;
        Camera25DLayerSorter::sort(current_3d_bin);
        for (const auto& item : current_3d_bin) {
            append_item(item);
        }
        current_3d_bin.clear();
    };

    for (const auto& resolved_layer : resolved.layers) {
        const Layer& layer = *resolved_layer.layer;

        if (!layer.active_at(ctx.frame)) {
            continue;
        }

        if (layer.kind == LayerKind::Adjustment) {
            flush_3d_bin();
            if (!layer.effects.empty()) {
                auto adj = graph.add_node(std::make_unique<AdjustmentNode>(layer.effects));
                graph.connect(current, adj);
                current = adj;
            }
            continue;
        }

        if (layer.kind == LayerKind::Null) {
            flush_3d_bin();
            continue;
        }

        if (cam25d.enabled && layer.is_3d) {
            Transform effective_transform = resolved_layer.world_transform;
            auto proj = project_layer_2_5d(
                effective_transform,
                cam25d,
                static_cast<f32>(ctx.width),
                static_cast<f32>(ctx.height)
            );

            if (proj.visible) {
                current_3d_bin.push_back({
                    .layer = &layer,
                    .transform = proj.transform,
                    .projection_matrix = proj.projection_matrix,
                    .depth = proj.depth,
                    .projected = true,
                    .insertion_index = resolved_layer.insertion_index
                });
            }
        } else {
            flush_3d_bin();
            append_item({
                .layer = &layer,
                .transform = resolved_layer.world_transform,
                .depth = 0.0f,
                .projected = false,
                .insertion_index = resolved_layer.insertion_index
            });
        }
    }

    flush_3d_bin();
    graph.set_output(current);
    return graph;
}

} // namespace chronon3d::graph::detail
