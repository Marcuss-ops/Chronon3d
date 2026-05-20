#include "graph_builder_source_pass.hpp"
#include "../graph_builder_coordinates.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <memory>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

GraphNodeId append_source_pass(RenderGraph& graph, const LayerGraphItem& item,
                               const RenderGraphContext& ctx) {
    const Layer& layer = *item.layer;

    if (layer.kind == LayerKind::Adjustment) {
        return k_invalid_node;
    }

    if (layer.kind == LayerKind::Normal) {
        GraphNodeId layer_output = graph.add_node(std::make_unique<ClearNode>());

        const bool layer_needs_transform = item.projected
            || layer.kind == LayerKind::Precomp
            || layer.kind == LayerKind::Video
            || item.transform.any();
        const bool use_local = ctx.modular_coordinates && layer_needs_transform && !item.native_3d;

        for (const auto& node : layer.nodes) {
            GraphNodeId source;
            if (node.shape.type == ShapeType::Text) {
                // Text Freezing: separate content from transform to allow cross-frame caching
                cache::NodeCacheKey source_key{
                    .scope = "layer.source.frozen:" + std::string(layer.name) + ":" + std::string(node.name),
                    .frame = 0, // Content is frame-invariant
                    .width = ctx.width,
                    .height = ctx.height,
                    .params_hash = chronon3d::graph::hash_render_node_content_only(node),
                    .source_hash = hash_string(node.name)
                };

                // Create a frozen source node at origin
                auto frozen_source = graph.add_node(std::make_unique<SourceNode>(
                    std::string(node.name) + ":frozen", node, source_key,
                    true,  // Centered
                    false, // 2D source for text raster
                    Mat4(1.0f), // Identity
                    1.0f  // Full opacity for source
                ));
                graph.node(frozen_source).set_frame_dependent(false);

                // Add TransformNode to apply the actual animated transform (local relative to layer if needed)
                const Mat4 text_matrix = use_local
                    ? (layer.hierarchy_resolved ? node.world_transform.to_mat4() 
                                                : (glm::inverse(item.world_matrix) * node.world_transform.to_mat4()))
                    : node.world_transform.to_mat4();
                const f32 layer_opacity = item.projected ? layer.transform.opacity : item.transform.opacity;
                const f32 text_opacity = use_local
                    ? (layer.hierarchy_resolved ? node.world_transform.opacity 
                                                : (node.world_transform.opacity / std::max(layer_opacity, 0.0001f)))
                    : node.world_transform.opacity;

                source = graph.add_node(std::make_unique<TransformNode>(
                    text_matrix,
                    text_opacity,
                    layer.cache_static ? Frame{0} : Frame{-1}
                ));
                graph.connect(frozen_source, source);
                graph.node(source).set_frame_dependent(!layer.cache_static);
            } else {
                // Standard path for other nodes
                cache::NodeCacheKey source_key{
                    .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(node.name),
                    .frame = layer.cache_static ? Frame{0} : ctx.frame,
                    .width = ctx.width,
                    .height = ctx.height,
                    .params_hash = hash_render_node(node),
                    .source_hash = hash_string(node.name)
                };

                const Mat4 shape_matrix = use_local
                    ? (layer.hierarchy_resolved ? node.world_transform.to_mat4() 
                                                : (glm::inverse(item.world_matrix) * node.world_transform.to_mat4()))
                    : node.world_transform.to_mat4();
                const f32 layer_opacity = item.projected ? layer.transform.opacity : item.transform.opacity;
                const f32 shape_opacity = use_local
                    ? (layer.hierarchy_resolved ? node.world_transform.opacity 
                                                : (node.world_transform.opacity / std::max(layer_opacity, 0.0001f)))
                    : node.world_transform.opacity;

                source = graph.add_node(std::make_unique<SourceNode>(
                    std::string(node.name), node, source_key,
                    should_use_centered_rendering(item, ctx),
                    item.projected,
                    ctx.modular_coordinates ? std::optional<Mat4>(shape_matrix) : std::nullopt,
                    ctx.modular_coordinates ? std::optional<f32>(shape_opacity) : std::nullopt
                ));
                graph.node(source).set_frame_dependent(!layer.cache_static);
            }

            auto composite = graph.add_node(std::make_unique<CompositeNode>(chronon3d::BlendMode::Normal));
            graph.node(composite).set_frame_dependent(!layer.cache_static);
            graph.connect(layer_output, composite);
            graph.connect(source, composite);
            layer_output = composite;
        }
        return layer_output;
    }

    if (layer.kind == LayerKind::Precomp) {
        auto precomp_id = graph.add_node(std::make_unique<PrecompNode>(
            std::string(layer.precomp_composition_name), layer.from, layer.duration,
            layer.cache_static ? Frame{0} : Frame{-1}
        ));
        graph.node(precomp_id).set_frame_dependent(!layer.cache_static);
        return precomp_id;
    }

    if (layer.kind == LayerKind::Video && layer.video_source) {
        auto video_id = graph.add_node(std::make_unique<VideoNode>(
            *layer.video_source, ctx.video_decoder, layer.from
        ));
        graph.node(video_id).set_frame_dependent(true);
        return video_id;
    }

    return graph.add_node(std::make_unique<ClearNode>());
}

} // namespace chronon3d::graph::detail
