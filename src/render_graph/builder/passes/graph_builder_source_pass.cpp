#include "graph_builder_source_pass.hpp"
#include "../graph_builder_coordinates.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#ifdef CHRONON_WITH_VIDEO
#include <chronon3d/render_graph/nodes/video_node.hpp>
#endif
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

        for (const auto& node : layer.nodes) {
            cache::NodeCacheKey source_key{
                .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(node.name),
                .frame = ctx.frame,
                .width = ctx.width,
                .height = ctx.height,
                .params_hash = hash_render_node(node),
                .source_hash = hash_bytes(node.name.data(), node.name.size())
            };

            std::optional<Mat4> rel_matrix;
            std::optional<f32> rel_opacity;
            if (ctx.modular_coordinates) {
                rel_matrix = node.world_transform.to_mat4();
                rel_opacity = node.world_transform.opacity;
            }

            auto source = graph.add_node(std::make_unique<SourceNode>(
                std::string(node.name), node, source_key,
                should_use_centered_rendering(item, ctx),
                item.projected,
                rel_matrix,
                rel_opacity
            ));

            auto composite = graph.add_node(std::make_unique<CompositeNode>(chronon3d::BlendMode::Normal));
            graph.connect(layer_output, composite);
            graph.connect(source, composite);
            layer_output = composite;
        }
        return layer_output;
    }

    if (layer.kind == LayerKind::Precomp) {
        return graph.add_node(std::make_unique<PrecompNode>(
            std::string(layer.precomp_composition_name), layer.from, layer.duration
        ));
    }

#ifdef CHRONON_WITH_VIDEO
    if (layer.kind == LayerKind::Video) {
        return graph.add_node(std::make_unique<VideoNode>(
            layer.video_source, ctx.video_decoder, layer.from
        ));
    }
#endif

    return graph.add_node(std::make_unique<ClearNode>());
}

} // namespace chronon3d::graph::detail
