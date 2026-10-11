#include "graph_builder_lighting_passes.hpp"
#include "graph_builder_source_pass.hpp"
#include "graph_builder_layer_passes.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/lighting_node.hpp>
#include <chronon3d/render_graph/nodes/shadow_node.hpp>
#include <chronon3d/render_graph/nodes/depth_grade_node.hpp>

#include <cmath>
#include <algorithm>
#include <string>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

// ── lighting pass ─────────────────────────────────────────────────

void append_lighting_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                    const LayerGraphItem& item,
                                    const RenderGraphContext& ctx,
                                    const BuilderContext& node_ctx) {
    const Layer& layer = *item.layer;

    if (!item.projected) {
        return;
    }
    if (!ctx.frame_input.light_context.enabled && !ctx.frame_input.light_context.has_point_lights()) {
        return;
    }
    if (item.native_3d) {
        return;
    }
    if (!layer.material().accepts_lights) {
        return;
    }

    auto lighting_node = graph.add_node(
        LightingNode::create(std::string(layer.name), item.world_matrix, layer.material()), node_ctx);
    graph.connect(layer_output, lighting_node);
    layer_output = lighting_node;
}

// ── depth grade pass ──────────────────────────────────────────────

void append_depth_grade_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                       const LayerGraphItem& item,
                                       const RenderGraphContext& ctx,
                                       const rendering::DepthGrade& grade,
                                       const BuilderContext& node_ctx) {
    if (!grade.enabled) return;
    if (!item.projected) return;
    if (!ctx.frame_input.light_context.enabled) return;

    auto grade_node = graph.add_node(
        DepthGradeNode::create(grade, item.world_z, item.layer->material().accepts_lights), node_ctx);
    graph.connect(layer_output, grade_node);
    layer_output = grade_node;
}

// ── shadow pass ───────────────────────────────────────────────────

void append_shadow_passes_if_needed(
    RenderGraph& graph,
    GraphNodeId& receiver_output,
    const LayerGraphItem& receiver_item,
    std::span<const ShadowCasterInfo> casters,
    const RenderGraphContext& ctx,
    const BuilderContext& node_ctx)
{
    if (!receiver_item.layer->material().accepts_shadows) return;
    if (!receiver_item.projected) return;
    const auto& lights = ctx.frame_input.light_context;
    const bool has_directional_shadow = lights.enabled && lights.directional_enabled;
    const bool has_layer_shadow = std::any_of(lights.point_lights.begin(), lights.point_lights.end(),
        [](const auto& light) { return light.casts_shadow; });
    if (!has_directional_shadow && !has_layer_shadow) return;

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

        BuilderContext caster_ctx{.layer_id = std::string(caster.layer->name)};
        GraphNodeId caster_out = append_source_pass(graph, caster_item, ctx, caster_ctx);
        if (caster_out == k_invalid_node) continue;
        append_transform_pass_if_needed(graph, caster_out, caster_item, ctx, caster_ctx);

        const auto add_shadow = [&](Vec3 travel, rendering::ShadowSettings settings,
                                    f32 bias, std::string_view suffix) {
            auto shadow_id = graph.add_node(ShadowNode::create(
                std::string(caster.layer->name) + std::string(suffix),
                caster.world_z,
                receiver_item.world_z,
                travel,
                settings,
                bias
            ), node_ctx);
            graph.connect(caster_out, shadow_id);

            auto composite_id = graph.add_node(
                std::make_unique<CompositeNode>(graph.next_composite_id(), BlendMode::Normal), node_ctx);
            graph.connect(receiver_output, composite_id);
            graph.connect(shadow_id,       composite_id);
            receiver_output = composite_id;
        };

        if (has_directional_shadow &&
            std::abs(caster.world_z - receiver_item.world_z) >= 1e-3f) {
            add_shadow(lights.direction, lights.shadows, 0.0f, "/directional-shadow");
        }

        const Vec3 caster_position{
            caster.world_matrix[3][0], caster.world_matrix[3][1], caster.world_z};
        for (std::size_t light_index = 0; light_index < lights.point_lights.size(); ++light_index) {
            const auto& light = lights.point_lights[light_index];
            if (!light.casts_shadow) continue;
            Vec3 travel = light.direction;
            rendering::ShadowSettings settings = lights.shadows;
            f32 bias = light.shadow_bias;
            if (light.kind != rendering::PointLightKind::Directional) {
                const Vec3 delta = caster_position - light.position;
                const f32 distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
                if (distance <= 1e-5f || (light.range > 0.0f && distance > light.range)) continue;
                travel = delta / distance;
                if (light.kind == rendering::PointLightKind::Spot) {
                    const Vec3 axis = rendering::safe_normalize(light.direction);
                    const f32 cone = glm::dot(travel, axis);
                    const f32 outer = std::cos(light.angle);
                    const f32 inner = std::cos(light.angle * (1.0f - light.penumbra));
                    if (cone <= outer) continue;
                    if (cone < inner && inner > outer) {
                        const f32 t = (cone - outer) / (inner - outer);
                        settings.opacity *= t * t * (3.0f - 2.0f * t);
                    }
                }
            }
            settings.blur_radius = light.shadow_radius;
            settings.contact_blur_radius = light.shadow_radius;
            settings.ambient_blur_radius = light.shadow_radius;
            if (std::abs(caster.world_z - receiver_item.world_z) < 1e-3f &&
                bias <= 0.0f) continue;
            add_shadow(travel, settings, bias,
                       "/light-shadow-" + std::to_string(light_index));
        }
    }
}

} // namespace chronon3d::graph::detail
