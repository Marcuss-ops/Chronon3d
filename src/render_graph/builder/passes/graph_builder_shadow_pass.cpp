#include "graph_builder_shadow_pass.hpp"

#include "graph_builder_source_pass.hpp"
#include "graph_builder_transform_pass.hpp"

#include <chronon3d/render_graph/nodes/shadow_node.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>

#include <cmath>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

void append_shadow_passes_if_needed(
    RenderGraph& graph,
    GraphNodeId& receiver_output,
    const LayerGraphItem& receiver_item,
    std::span<const ShadowCasterInfo> casters,
    const RenderGraphContext& ctx)
{
    if (!receiver_item.layer->material.accepts_shadows) return;
    if (!ctx.light_context.enabled || !ctx.light_context.directional_enabled) return;
    if (!receiver_item.projected) return;

    for (const auto& caster : casters) {
        if (!caster.layer->material.casts_shadows) continue;
        if (!caster.projected) continue;
        if (caster.layer == receiver_item.layer) continue;
        if (std::abs(caster.world_z - receiver_item.world_z) < 1e-3f) continue;

        // Build caster source + transform on-demand (may duplicate vs. main pass — V1 trade-off)
        LayerGraphItem caster_item{
            .layer             = caster.layer,
            .world_matrix      = caster.world_matrix,
            .projection_matrix = caster.projection_matrix,
            .world_z           = caster.world_z,
            .projected         = caster.projected,
        };

        GraphNodeId caster_out = append_source_pass(graph, caster_item, ctx);
        if (caster_out == k_invalid_node) continue;
        append_transform_pass_if_needed(graph, caster_out, caster_item, ctx);

        // ShadowNode: translates + blurs the caster silhouette
        auto shadow_id = graph.add_node(ShadowNode::create(
            std::string(caster.layer->name),
            caster.world_z,
            receiver_item.world_z,
            ctx.light_context.direction,
            ctx.light_context.shadows
        ));
        graph.connect(caster_out, shadow_id);

        // Composite shadow (dark transparent) over receiver
        auto composite_id = graph.add_node(
            std::make_unique<CompositeNode>(BlendMode::Normal));
        graph.connect(receiver_output, composite_id);
        graph.connect(shadow_id,       composite_id);
        receiver_output = composite_id;
    }
}

} // namespace chronon3d::graph::detail
