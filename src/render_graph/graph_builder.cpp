#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/render_graph/nodes/precomp_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/scene/layer.hpp>

namespace chronon3d::graph {

RenderGraph GraphBuilder::build(const Scene& scene, const RenderGraphContext& ctx) {
    RenderGraph graph;

    GraphNodeId current = graph.add_node(std::make_unique<ClearNode>());

    // Root scene nodes (direct RenderNodes on the Scene, not in any layer)
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

        auto composite = graph.add_node(std::make_unique<CompositeNode>(BlendMode::Normal));
        graph.connect(current, composite);
        graph.connect(source, composite);
        current = composite;
    }

    // Layers, bottom to top
    for (const auto& layer : scene.layers()) {
        if (!layer.active_at(ctx.frame)) continue;

        if (layer.kind == LayerKind::Null) {
            continue;
        }

        if (layer.kind == LayerKind::Normal || 
            layer.kind == LayerKind::Precomp ||
            layer.kind == LayerKind::Video) {

            GraphNodeId layer_output;

            if (layer.kind == LayerKind::Normal) {
                layer_output = build_layer_source(graph, layer, ctx);
            } else if (layer.kind == LayerKind::Precomp) {
                layer_output = graph.add_node(std::make_unique<PrecompNode>(
                    std::string(layer.precomp_composition_name),
                    layer.from,
                    layer.duration
                ));
            } else {
                layer_output = graph.add_node(std::make_unique<VideoNode>(
                    layer.video_source,
                    ctx.video_decoder,
                    layer.from
                ));
            }

            if (layer.kind == LayerKind::Precomp || 
                layer.kind == LayerKind::Video ||
                layer.transform.any()) {
                auto transform = graph.add_node(std::make_unique<TransformNode>(layer.transform));
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

            auto composite = graph.add_node(std::make_unique<CompositeNode>(layer.blend_mode));
            graph.connect(current, composite);
            graph.connect(layer_output, composite);
            current = composite;

        } else if (layer.kind == LayerKind::Adjustment) {
            auto adj = graph.add_node(std::make_unique<AdjustmentNode>(layer.effects));
            graph.connect(current, adj);
            current = adj;
        }
    }

    graph.set_output(current);
    return graph;
}

GraphNodeId GraphBuilder::build_layer_source(
    RenderGraph& graph,
    const Layer& layer,
    const RenderGraphContext& ctx
) {
    GraphNodeId layer_current = graph.add_node(std::make_unique<ClearNode>());

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

        auto composite = graph.add_node(std::make_unique<CompositeNode>(BlendMode::Normal));
        graph.connect(layer_current, composite);
        graph.connect(source, composite);
        layer_current = composite;
    }

    return layer_current;
}

} // namespace chronon3d::graph
