#include "graph_builder_layer_pipeline.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#ifdef CHRONON_WITH_VIDEO
#include <chronon3d/render_graph/nodes/video_node.hpp>
#endif
#include <chronon3d/render_graph/render_graph_hashing.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

GraphNodeId LayerPipelineBuilder::append_root_sources(RenderGraph& graph, const Scene& scene,
                                                      const RenderGraphContext& ctx,
                                                      GraphNodeId current) {
    for (const auto& node : scene.nodes()) {
        cache::NodeCacheKey source_key{
            .scope = "root.source:" + std::string(node.name),
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_render_node(node),
            .source_hash = hash_bytes(node.name.data(), node.name.size())
        };

        auto source = graph.add_node(std::make_unique<SourceNode>(
            std::string(node.name), node, source_key
        ));

        auto composite = graph.add_node(std::make_unique<CompositeNode>(chronon3d::BlendMode::Normal));
        graph.connect(current, composite);
        graph.connect(source, composite);
        current = composite;
    }

    return current;
}

void LayerPipelineBuilder::append_layer_pipeline(RenderGraph& graph, const LayerGraphItem& item,
                                                 GraphNodeId& current, const RenderGraphContext& ctx,
                                                 const Camera2_5D& cam25d) {
    const Layer& layer = *item.layer;

    GraphNodeId layer_output;

    if (layer.kind == LayerKind::Normal) {
        layer_output = graph.add_node(std::make_unique<ClearNode>());

        for (const auto& node : layer.nodes) {
            cache::NodeCacheKey source_key{
                .scope = "layer.source:" + std::string(layer.name) + ":" + std::string(node.name),
                .frame = ctx.frame,
                .width = ctx.width,
                .height = ctx.height,
                .params_hash = hash_render_node(node),
                .source_hash = hash_bytes(node.name.data(), node.name.size())
            };

            auto source = graph.add_node(std::make_unique<SourceNode>(
                std::string(node.name), node, source_key
            ));

            auto composite = graph.add_node(std::make_unique<CompositeNode>(chronon3d::BlendMode::Normal));
            graph.connect(layer_output, composite);
            graph.connect(source, composite);
            layer_output = composite;
        }
    } else if (layer.kind == LayerKind::Precomp) {
        layer_output = graph.add_node(std::make_unique<PrecompNode>(
            std::string(layer.precomp_composition_name), layer.from, layer.duration
        ));
#ifdef CHRONON_WITH_VIDEO
    } else {
        layer_output = graph.add_node(std::make_unique<VideoNode>(
            layer.video_source, ctx.video_decoder, layer.from
        ));
    }
#else
    } else {
        layer_output = graph.add_node(std::make_unique<ClearNode>());
    }
#endif

    const bool needs_transform = item.projected
        || layer.kind == LayerKind::Precomp
        || layer.kind == LayerKind::Video
        || item.transform.any();

    if (needs_transform) {
        std::unique_ptr<TransformNode> transform_node;
        if (item.projected) {
            transform_node = std::make_unique<TransformNode>(item.projection_matrix,
                                                             layer.transform.opacity);
        } else {
            transform_node = std::make_unique<TransformNode>(item.transform);
        }

        auto transform = graph.add_node(std::move(transform_node));
        graph.connect(layer_output, transform);
        layer_output = transform;
    }

    if (layer.mask.enabled()) {
        auto masked = graph.add_node(std::make_unique<MaskNode>(layer.mask));
        graph.connect(layer_output, masked);
        layer_output = masked;
    }

    if (!layer.effects.empty()) {
        auto effects = graph.add_node(std::make_unique<EffectStackNode>(layer.effects));
        graph.connect(layer_output, effects);
        layer_output = effects;
    }

    if (item.projected && cam25d.dof.enabled) {
        const f32 world_z = item.depth + cam25d.position.z;
        const f32 dist = std::abs(world_z - cam25d.dof.focus_z);
        const f32 blur = std::min(dist * cam25d.dof.aperture, cam25d.dof.max_blur);

        if (blur > 0.5f) {
            EffectStack dof_stack;
            dof_stack.push_back(EffectInstance{BlurParams{blur}});
            auto dof_node = graph.add_node(std::make_unique<EffectStackNode>(std::move(dof_stack)));
            graph.connect(layer_output, dof_node);
            layer_output = dof_node;
        }
    }

    auto composite = graph.add_node(std::make_unique<CompositeNode>(layer.blend_mode));
    graph.connect(current, composite);
    graph.connect(layer_output, composite);
    current = composite;
}

} // namespace chronon3d::graph::detail
