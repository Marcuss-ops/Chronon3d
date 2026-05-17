#include "graph_builder_pipeline.hpp"

#include "utils/graph_builder_camera25d_sorter.hpp"
#include "graph_builder_layer_pipeline.hpp"

#include <iostream>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/shape.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

// Native-3D shapes handle their own screen-space projection internally.
// They must use an identity projection_matrix so TransformNode is a pass-through.
static bool is_native_3d_layer(const Layer& layer) {
    for (const auto& node : layer.nodes) {
        if (node.shape.type == ShapeType::FakeBox3D  ||
            node.shape.type == ShapeType::GridPlane   ||
            node.shape.type == ShapeType::FakeExtrudedText) {
            return true;
        }
    }
    return false;
}

RenderGraph build_graph(const Scene& scene, const RenderGraphContext& ctx,
                        const LayerResolutionResult& resolved) {
    RenderGraph graph;
    GraphNodeId current = graph.add_node(std::make_unique<ClearNode>());
    current = LayerPipelineBuilder::append_root_sources(graph, scene, ctx, current);

    const Camera2_5DRuntime& cam25d = resolved.camera.camera;

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


        if (layer.kind == LayerKind::Null) {
            flush_3d_bin();
            continue;
        }

        if (cam25d.enabled && layer.is_3d) {
            Transform effective_transform = resolved_layer.world_transform;
            if (!ctx.modular_coordinates) {
                effective_transform.position.x -= ctx.width * 0.5f;
                effective_transform.position.y -= ctx.height * 0.5f;
            }
            auto proj = project_layer_2_5d(
                effective_transform,
                cam25d,
                static_cast<f32>(ctx.width),
                static_cast<f32>(ctx.height)
            );

            if (proj.visible) {
                // Native-3D shapes (FakeBox3D, GridPlane, FakeExtrudedText) project
                // directly to screen coords in SourceNode. TransformNode must be
                // identity (pass-through) to avoid double-projection.
                const Mat4 eff_proj = is_native_3d_layer(layer)
                    ? Mat4(1.0f)
                    : proj.projection_matrix;

                current_3d_bin.push_back({
                    .layer = &layer,
                    .transform = proj.transform,
                    .projection_matrix = eff_proj,
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
