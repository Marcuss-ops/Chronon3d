#include "graph_builder_pipeline.hpp"

#include "utils/graph_builder_camera25d_sorter.hpp"
#include "graph_builder_layer_pipeline.hpp"

#include <iostream>
#include <unordered_set>
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

    // Collect names of layers used as matte sources — they are not rendered directly.
    std::unordered_set<std::string> matte_source_names;
    for (const auto& rl : resolved.layers) {
        const Layer& l = *rl.layer;
        if (l.track_matte.active()) {
            matte_source_names.insert(std::string(l.track_matte.source_layer));
        }
    }

    // Build a lookup: layer name → LayerGraphItem (2D path only; 3D resolved in the loop).
    // Used to find matte sources at graph-build time.
    std::unordered_map<std::string, const ResolvedLayer*> name_to_resolved;
    for (const auto& rl : resolved.layers) {
        name_to_resolved[std::string(rl.layer->name)] = &rl;
    }

    // Helper: build a LayerGraphItem for a 2D layer (no projection).
    auto make_2d_item = [&](const ResolvedLayer& rl) -> LayerGraphItem {
        return LayerGraphItem{
            .layer = rl.layer,
            .transform = rl.world_transform,
            .depth = 0.0f,
            .projected = false,
            .insertion_index = rl.insertion_index,
        };
    };

    auto append_item = [&](LayerGraphItem item) {
        // Pre-build the matte sub-pipeline if this layer has a track matte.
        if (item.layer->track_matte.active()) {
            const std::string src_name(item.layer->track_matte.source_layer);
            auto it = name_to_resolved.find(src_name);
            if (it != name_to_resolved.end() && it->second->layer->active_at(ctx.frame)) {
                LayerGraphItem matte_item = make_2d_item(*it->second);
                item.matte_node = LayerPipelineBuilder::build_matte_sub_pipeline(graph, matte_item, ctx);
            }
        }
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

        // Matte source layers are consumed exclusively by TrackMatteNode.
        if (matte_source_names.count(std::string(layer.name))) {
            flush_3d_bin();
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
