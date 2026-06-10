#include "graph_builder_pipeline.hpp"

#include "passes/graph_builder_layer_passes.hpp"
#include "passes/graph_builder_lighting_passes.hpp"
#include "passes/graph_builder_source_pass.hpp"
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/track_matte_node.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

#include <algorithm>

namespace chronon3d::graph::detail {

GraphNodeId append_root_sources(RenderGraph& graph, const Scene& scene,
                                RenderGraphContext& ctx,
                                GraphNodeId current) {
    bool first_root_source = true;
    for (const auto& node : scene.nodes()) {
        cache::NodeCacheKey source_key{
            .scope = "root.source:" + std::string(node.name),
            .frame = ctx.frame.frame.frame,
            .width = ctx.frame.frame.width,
            .height = ctx.frame.frame.height,
            .params_hash = hash_render_node(node),
            .source_hash = hash_bytes(node.name.data(), node.name.size())
        };

        auto source = graph.add_node(std::make_unique<SourceNode>(
            std::string(node.name), node, source_key,
            ctx.options.modular_coordinates
        ));

        if (first_root_source) {
            if (const auto* source_node = dynamic_cast<const SourceNode*>(&graph.node(source));
                source_node && source_node->can_seed_full_frame(ctx)) {
                ctx.options.skip_initial_clear = true;
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

void append_layer_pipeline(RenderGraph& graph, const LayerGraphItem& item,
                           GraphNodeId& current, RenderGraphContext& ctx,
                           const Camera2_5DRuntime& cam25d,
                           std::span<const ShadowCasterInfo> casters,
                           const rendering::DepthGrade& depth_grade) {
    std::string prev_layer = g_current_builder_layer_id;
    g_current_builder_layer_id = std::string(item.layer->name);

    GraphNodeId layer_output = append_source_pass(graph, item, ctx);
    const Layer& layer = *item.layer;

    if (!ctx.options.skip_initial_clear && layer_output != k_invalid_node) {
        const bool simple_opaque_full_frame_layer =
            layer.kind == LayerKind::Normal &&
            layer.nodes.size() == 1 &&
            layer.mask.type == MaskType::None &&
            layer.effects.empty() &&
            layer.blend_mode == BlendMode::Normal &&
            !layer.track_matte.active() &&
            (layer.transition_in.transition_id.empty() || layer.transition_in.transition_id == "none") &&
            (layer.transition_out.transition_id.empty() || layer.transition_out.transition_id == "none") &&
            !layer.nodes[0].shadow.enabled &&
            !layer.nodes[0].glow.enabled;

        const auto* source_node = dynamic_cast<const SourceNode*>(&graph.node(layer_output));
        const bool can_seed = source_node && source_node->can_seed_full_frame(ctx);

        if (simple_opaque_full_frame_layer && can_seed) {
            ctx.options.skip_initial_clear = true;
        }
    }

    if (layer.kind == LayerKind::Adjustment) {
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

    const bool mask_before_transform =
        layer.kind == LayerKind::Normal ||
        layer.kind == LayerKind::Precomp ||
        layer.kind == LayerKind::Video;

    if (mask_before_transform) {
        append_mask_pass_if_needed(graph, layer_output, item, ctx);
    }

    append_transform_pass_if_needed(graph, layer_output, item, ctx);

    if (!mask_before_transform) {
        append_mask_pass_if_needed(graph, layer_output, item, ctx);
    }

    append_lighting_pass_if_needed(graph, layer_output, item, ctx);
    append_shadow_passes_if_needed(graph, layer_output, item, casters, ctx);
    append_depth_grade_pass_if_needed(graph, layer_output, item, ctx, depth_grade);
    append_effect_pass_if_needed(graph, layer_output, *item.layer, item, cam25d, ctx);

    if (layer.track_matte.active() && item.matte_node != k_invalid_node) {
        cache::NodeCacheKey matte_key{
            .scope       = "matte:" + std::string(layer.name),
            .frame       = (layer.cache_static || item.is_static) ? Frame{0} : ctx.frame.frame.frame,
            .width       = ctx.frame.frame.width,
            .height      = ctx.frame.frame.height,
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
        graph.connect(layer_output, matte_node);
        graph.connect(item.matte_node, matte_node);
        layer_output = matte_node;
    }

    const bool has_in_trans = !layer.transition_in.transition_id.empty() && layer.transition_in.transition_id != "none";
    const bool has_out_trans = !layer.transition_out.transition_id.empty() && layer.transition_out.transition_id != "none";

    if (has_in_trans || has_out_trans) {
        std::string trans_id = "none";
        LayerTransitionSpec active_spec;
        bool is_out = false;

        if (has_in_trans) {
            trans_id = layer.transition_in.transition_id;
            active_spec = layer.transition_in;
            is_out = false;
        } else if (has_out_trans) {
            trans_id = layer.transition_out.transition_id;
            active_spec = layer.transition_out;
            is_out = true;
        }

        if (trans_id != "none") {
            auto trans_node = graph.add_node(std::make_unique<TransitionNode>(
                std::string(layer.name), active_spec, is_out, layer.from, layer.duration
            ));
            graph.node(trans_node).set_frame_dependent(true);
            graph.connect(layer_output, trans_node);
            layer_output = trans_node;
        }
    }

    append_composite_pass(graph, current, layer_output, *item.layer, (layer.cache_static || item.is_static), ctx, item.world_z);
    g_current_builder_layer_id = prev_layer;
}

void sort_camera25d_layers(std::vector<LayerGraphItem>& items) {
    std::stable_sort(items.begin(), items.end(),
        [](const LayerGraphItem& a, const LayerGraphItem& b) {
            if (a.depth != b.depth) return a.depth > b.depth;
            return a.insertion_index < b.insertion_index;
        });
}

} // namespace chronon3d::graph::detail
