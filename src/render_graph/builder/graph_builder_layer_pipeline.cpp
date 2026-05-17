#include "graph_builder_layer_pipeline.hpp"

#include "passes/graph_builder_source_pass.hpp"
#include "passes/graph_builder_transform_pass.hpp"
#include "passes/graph_builder_mask_pass.hpp"
#include "passes/graph_builder_effect_pass.hpp"
#include "passes/graph_builder_composite_pass.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/effects/effect_registry.hpp>

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
                                                 const Camera2_5DRuntime& cam25d) {
    // 1. Source pass — render layer content
    GraphNodeId layer_output = append_source_pass(graph, item, ctx);

    const Layer& layer = *item.layer;
    if (layer.kind == LayerKind::Adjustment) {
        // Apply effects directly on current node, no source or composite needed
        for (const auto& eff : layer.effects) {
            auto node = chronon3d::effects::EffectRegistry::instance().create_node(eff);
            GraphNodeId effect_id = graph.add_node(std::move(node));
            graph.connect(current, effect_id);
            current = effect_id;
        }
        return;
    }

    // 2. For non-projected 2D Normal layers: apply mask before transform so that mask.pos=(0,0)
    //    aligns with the SourceNode's centered-coordinate output (layer center = canvas center).
    //    For projected/precomp/video layers: mask is applied after transform in world-pixel space.
    const bool mask_before_transform =
        layer.kind == LayerKind::Normal ||
        layer.kind == LayerKind::Precomp ||
        layer.kind == LayerKind::Video;

    if (mask_before_transform)
        append_mask_pass_if_needed(graph, layer_output, *item.layer);

    // 3. Transform pass — place the layer at its world position
    append_transform_pass_if_needed(graph, layer_output, item, ctx);

    // 4. For all other cases: apply mask after transform in pixel space
    if (!mask_before_transform)
        append_mask_pass_if_needed(graph, layer_output, *item.layer);

    // 5. Effect pass — apply effect stack + optional DOF blur
    append_effect_pass_if_needed(graph, layer_output, *item.layer, item, cam25d);

    // 6. Composite pass — blend into the current frame buffer
    append_composite_pass(graph, current, layer_output, *item.layer);
}

} // namespace chronon3d::graph::detail
