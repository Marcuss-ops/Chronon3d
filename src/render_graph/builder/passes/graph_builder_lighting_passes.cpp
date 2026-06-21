#include "graph_builder_lighting_passes.hpp"
#include "graph_builder_source_pass.hpp"
#include "graph_builder_layer_passes.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/lighting_node.hpp>
#include <chronon3d/render_graph/nodes/shadow_node.hpp>
#include <chronon3d/render_graph/nodes/depth_grade_node.hpp>

#include <cmath>
#include <string>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

// ── lighting pass ─────────────────────────────────────────────────

void append_lighting_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                    const LayerGraphItem& item,
                                    const RenderGraphContext& ctx) {
    const Layer& layer = *item.layer;

    if (!item.projected) {
        return;
    }
    if (!ctx.frame_input.light_context.enabled) {
        return;
    }
    if (item.native_3d) {
        return;
    }
    if (!layer.material().accepts_lights) {
        return;
    }

    auto lighting_node = graph.add_node(
        LightingNode::create(std::string(layer.name), item.world_matrix, layer.material()));
    graph.connect(layer_output, lighting_node);
    layer_output = lighting_node;
}

// ── depth grade pass ──────────────────────────────────────────────

void append_depth_grade_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                       const LayerGraphItem& item,
                                       const RenderGraphContext& ctx,
                                       const rendering::DepthGrade& grade) {
    if (!grade.enabled) return;
    if (!item.projected) return;
    if (!ctx.frame_input.light_context.enabled) return;

    auto grade_node = graph.add_node(
        DepthGradeNode::create(grade, item.world_z, item.layer->material().accepts_lights));
    graph.connect(layer_output, grade_node);
    layer_output = grade_node;
}

// ── shadow pass ───────────────────────────────────────────────────

void append_shadow_passes_if_needed(
    RenderGraph& graph,
    GraphNodeId& receiver_output,
    const LayerGraphItem& receiver_item,
    std::span<const ShadowCasterInfo> casters,
    const RenderGraphContext& ctx)
{
    if (!receiver_item.layer->material().accepts_shadows) return;
    if (!ctx.frame_input.light_context.enabled || !ctx.frame_input.light_context.directional_enabled) return;
    if (!receiver_item.projected) return;

    for (const auto& caster : casters) {
        if (!caster.layer->material().casts_shadows) continue;
        if (!caster.projected) continue;
        if (caster.layer == receiver_item.layer) continue;
        if (std::abs(caster.world_z - receiver_item.world_z) < 1e-3f) continue;

        // Build caster source + transform on-demand
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
            ctx.frame_input.light_context.direction,
            ctx.frame_input.light_context.shadows
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
