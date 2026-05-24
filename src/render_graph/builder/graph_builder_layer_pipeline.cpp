#include "graph_builder_layer_pipeline.hpp"

#include "passes/graph_builder_source_pass.hpp"
#include "passes/graph_builder_transform_pass.hpp"
#include "passes/graph_builder_lighting_pass.hpp"
#include "passes/graph_builder_shadow_pass.hpp"
#include "passes/graph_builder_mask_pass.hpp"
#include "passes/graph_builder_effect_pass.hpp"
#include "passes/graph_builder_composite_pass.hpp"

#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/animation/easing.hpp>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

GraphNodeId LayerPipelineBuilder::append_root_sources(RenderGraph& graph, const Scene& scene,
                                                      RenderGraphContext& ctx,
                                                      GraphNodeId current) {
    bool first_root_source = true;
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
            std::string(node.name), node, source_key,
            ctx.modular_coordinates
        ));

        if (first_root_source) {
            if (const auto* source_node = dynamic_cast<const SourceNode*>(&graph.node(source));
                source_node && source_node->can_seed_full_frame(ctx)) {
                ctx.skip_initial_clear = true;
            }
            first_root_source = false;
        }

        auto composite = graph.add_node(std::make_unique<CompositeNode>(chronon3d::BlendMode::Normal));
        graph.connect(current, composite);
        graph.connect(source, composite);
        current = composite;
    }

    return current;
}

GraphNodeId LayerPipelineBuilder::build_matte_sub_pipeline(
    RenderGraph& graph, const LayerGraphItem& item, const RenderGraphContext& ctx)
{
    std::string prev_layer = g_current_builder_layer_id;
    g_current_builder_layer_id = std::string(item.layer->name);
    GraphNodeId out = append_source_pass(graph, item, ctx);
    if (out == k_invalid_node) {
        g_current_builder_layer_id = prev_layer;
        return k_invalid_node;
    }
    append_transform_pass_if_needed(graph, out, item, ctx);
    g_current_builder_layer_id = prev_layer;
    return out;
}

void LayerPipelineBuilder::append_layer_pipeline(RenderGraph& graph, const LayerGraphItem& item,
                                                 GraphNodeId& current, RenderGraphContext& ctx,
                                                 const Camera2_5DRuntime& cam25d,
                                                 std::span<const ShadowCasterInfo> casters) {
    std::string prev_layer = g_current_builder_layer_id;
    g_current_builder_layer_id = std::string(item.layer->name);

    // 1. Source pass — render layer content
    GraphNodeId layer_output = append_source_pass(graph, item, ctx);
    const Layer& layer = *item.layer;

    if (!ctx.skip_initial_clear && layer_output != k_invalid_node) {
        const bool simple_opaque_full_frame_layer =
            layer.kind == LayerKind::Normal &&
            layer.nodes.size() == 1 &&
            layer.mask.type == MaskType::None &&
            layer.effects.empty() &&
            layer.blend_mode == BlendMode::Normal &&
            !layer.track_matte.active() &&
            layer.transition_in.transition_id.empty() &&
            layer.transition_out.transition_id.empty() &&
            !layer.nodes[0].shadow.enabled &&
            !layer.nodes[0].glow.enabled;

        if (simple_opaque_full_frame_layer) {
            if (const auto* source_node = dynamic_cast<const SourceNode*>(&graph.node(layer_output));
                source_node && source_node->can_seed_full_frame(ctx)) {
                ctx.skip_initial_clear = true;
            }
        }
    }

    if (layer.kind == LayerKind::Adjustment) {
        // Apply effects directly on current node, no source or composite needed
        for (const auto& eff : layer.effects) {
            chronon3d::EffectStack stack;
            stack.push_back(eff);
            auto node = std::make_unique<AdjustmentNode>(std::move(stack));
            GraphNodeId adj_id = graph.add_node(std::move(node));
            graph.node(adj_id).set_frame_dependent(!(layer.cache_static || item.is_static));
            graph.connect(current, adj_id);
            current = adj_id;
        }
        g_current_builder_layer_id = prev_layer;
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
        append_mask_pass_if_needed(graph, layer_output, item, ctx);

    // 3. Transform pass — place the layer at its world position
    append_transform_pass_if_needed(graph, layer_output, item, ctx);

    // 4. For all other cases: apply mask after transform in pixel space
    if (!mask_before_transform)
        append_mask_pass_if_needed(graph, layer_output, item, ctx);

    // 5. Lighting pass — diffuse shading for 3D projected layers with lights enabled.
    append_lighting_pass_if_needed(graph, layer_output, item, ctx);

    // 5b. Shadow pass — project caster silhouettes onto this layer if it accepts shadows.
    append_shadow_passes_if_needed(graph, layer_output, item, casters, ctx);

    // 6. Effect pass — apply effect stack + optional DOF blur
    append_effect_pass_if_needed(graph, layer_output, *item.layer, item, cam25d);

    // 7. Track matte pass — if a matte source node was pre-built, apply it
    if (layer.track_matte.active() && item.matte_node != k_invalid_node) {
        cache::NodeCacheKey matte_key{
            .scope       = "matte:" + std::string(layer.name),
            .frame       = (layer.cache_static || item.is_static) ? Frame{0} : ctx.frame,
            .width       = ctx.width,
            .height      = ctx.height,
            .params_hash = hash_bytes(layer.track_matte.source_layer.data(),
                                      layer.track_matte.source_layer.size()),
        };
        matte_key.params_hash = hash_combine(
            matte_key.params_hash,
            static_cast<u64>(layer.track_matte.type));

        auto matte_node = graph.add_node(
            std::make_unique<TrackMatteNode>(layer.track_matte.type,
                                              std::string(layer.name), matte_key));
        graph.node(matte_node).set_frame_dependent(!(layer.cache_static || item.is_static));
        graph.connect(layer_output,     matte_node);
        graph.connect(item.matte_node,  matte_node);
        layer_output = matte_node;
    }

    // 8. Transition pass — if a transition is active, apply it
    double global_time_seconds = ctx.time_seconds / ctx.fps;
    double layer_start_seconds = static_cast<double>(layer.from) / ctx.fps;
    double layer_time = global_time_seconds - layer_start_seconds;

    double layer_duration_seconds = (layer.duration >= 0)
        ? (static_cast<double>(layer.duration) / ctx.fps)
        : std::numeric_limits<double>::infinity();

    const bool has_in_trans = !layer.transition_in.transition_id.empty() && layer.transition_in.transition_id != "none";
    const bool has_out_trans = !layer.transition_out.transition_id.empty() && layer.transition_out.transition_id != "none";

    if (has_in_trans || has_out_trans) {
        std::string trans_id = "none";
        LayerTransitionSpec active_spec;
        bool is_out = false;
        double progress = 0.0;

        if (has_in_trans && layer_time < (layer.transition_in.delay + layer.transition_in.duration)) {
            if (layer_time >= layer.transition_in.delay) {
                trans_id = layer.transition_in.transition_id;
                active_spec = layer.transition_in;
                is_out = false;
                if (layer.transition_in.duration > 0.0) {
                    progress = (layer_time - layer.transition_in.delay) / layer.transition_in.duration;
                } else {
                    progress = 1.0;
                }
            } else {
                trans_id = layer.transition_in.transition_id;
                active_spec = layer.transition_in;
                is_out = false;
                progress = 0.0;
            }
        } else if (has_out_trans && std::isfinite(layer_duration_seconds)) {
            double trans_out_start = layer_duration_seconds - layer.transition_out.duration - layer.transition_out.delay;
            if (layer_time >= trans_out_start) {
                trans_id = layer.transition_out.transition_id;
                active_spec = layer.transition_out;
                is_out = true;
                if (layer_time < layer_duration_seconds - layer.transition_out.delay) {
                    if (layer.transition_out.duration > 0.0) {
                        progress = (layer_time - trans_out_start) / layer.transition_out.duration;
                    } else {
                        progress = 1.0;
                    }
                } else {
                    progress = 1.0;
                }
            }
        }

        if (trans_id != "none") {
            float eased_progress = easing::apply(active_spec.easing, static_cast<float>(std::clamp(progress, 0.0, 1.0)));
            auto trans_node = graph.add_node(std::make_unique<TransitionNode>(
                std::string(layer.name), active_spec, is_out, eased_progress
            ));
            graph.node(trans_node).set_frame_dependent(true);
            graph.connect(layer_output, trans_node);
            layer_output = trans_node;
        }
    }

    // 9. Composite pass — blend into the current frame buffer
    append_composite_pass(graph, current, layer_output, *item.layer, (layer.cache_static || item.is_static), ctx);
    g_current_builder_layer_id = prev_layer;
}

} // namespace chronon3d::graph::detail
