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
            .frame = ctx.frame_input.frame,
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = hash_render_node(node),
            .source_hash = hash_bytes(node.name.data(), node.name.size())
        };
        // TICKET-ae-cam-hash-collision Soluzione B — root-level source keys
        // ALSO need the camera fingerprint so a cinematic camera-driven
        // composition with mixed root/child sources stays deterministic.
        if (ctx.frame_input.has_camera_2_5d) {
            cache::fold_camera_into_params_hash(source_key, ctx.frame_input.camera_2_5d);
        }

        // Root nodes already carry top-left pixel placement in their
        // world transform (including the anchor baked by RenderNodeFactory).
        // A canvas translation here would shift every standalone shape by
        // half the frame and make dirty-region bboxes disagree with execute.
        std::optional<Mat4> root_matrix;
        if (ctx.policy.modular_coordinates && node.shape.type() == ShapeType::Line) {
            root_matrix = glm::translate(Mat4(1.0f), Vec3(
                ctx.frame_input.width * 0.5f,
                ctx.frame_input.height * 0.5f,
                0.0f));
        }
        auto source = graph.add_node(std::make_unique<SourceNode>(
            std::string(node.name), node, source_key,
            root_matrix,
            std::optional<f32>(node.world_transform.opacity)
        ));

        if (first_root_source) {
            if (const auto* source_node = dynamic_cast<const SourceNode*>(&graph.node(source));
                source_node && source_node->can_seed_full_frame(ctx)) {
                ctx.policy.skip_initial_clear = true;
            }
            first_root_source = false;
        }

        auto composite = graph.add_node(std::make_unique<CompositeNode>(graph.next_composite_id(), chronon3d::BlendMode::Normal));
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
    BuilderContext node_ctx{
        .layer_id = std::string(item.layer->name),
        .opacity_evaluator = [opacity = item.layer->anim_transform.opacity](const RenderFrameInfo& info) -> float {
            return opacity.evaluate(info.sample_time);
        }
    };

    GraphNodeId layer_output = append_source_pass(graph, item, ctx, node_ctx);
    const Layer& layer = *item.layer;

    if (!ctx.policy.skip_initial_clear && layer_output != k_invalid_node) {
        const bool simple_opaque_full_frame_layer =
            (layer.kind == LayerKind::Normal || layer.kind == LayerKind::Shape || layer.kind == LayerKind::Text) &&
            layer.nodes.size() == 1 &&
            layer.mask.type == MaskType::None &&
            layer.effects().empty() &&
            layer.blend_mode == BlendMode::Normal &&
            !layer.track_matte.active() &&
            (layer.transition_in.transition_id.empty() || layer.transition_in.transition_id == "none") &&
            (layer.transition_out.transition_id.empty() || layer.transition_out.transition_id == "none") &&
            !layer.nodes[0].shadow.enabled &&
            !layer.nodes[0].glow.enabled;

        const auto* source_node = dynamic_cast<const SourceNode*>(&graph.node(layer_output));
        const bool can_seed = source_node && source_node->can_seed_full_frame(ctx);

        if (simple_opaque_full_frame_layer && can_seed) {
            ctx.policy.skip_initial_clear = true;
        }
    }

    if (layer.kind == LayerKind::Adjustment) {
        const bool is_static = layer.cache_static || item.is_static;
        const auto policy = is_static ? static_memory_cache("adjustment") : frame_variant_cache("adjustment");
        for (const auto& eff : layer.effects()) {
            chronon3d::EffectStack stack;
            stack.push_back(eff);
            GraphNodeId adj_id = graph.add_node(std::make_unique<AdjustmentNode>(std::move(stack), policy), node_ctx);
            graph.connect(current, adj_id);
            current = adj_id;
        }
        return;
    }

    const bool mask_before_transform =
        layer.kind == LayerKind::Normal ||
        layer.kind == LayerKind::Shape ||
        layer.kind == LayerKind::Text ||
        layer.kind == LayerKind::Precomp ||
        layer.kind == LayerKind::Video;

    if (mask_before_transform) {
        append_mask_pass_if_needed(graph, layer_output, item, ctx, node_ctx);
    }

    append_transform_pass_if_needed(graph, layer_output, item, ctx, node_ctx);

    if (!mask_before_transform) {
        append_mask_pass_if_needed(graph, layer_output, item, ctx, node_ctx);
    }

    append_lighting_pass_if_needed(graph, layer_output, item, ctx, node_ctx);
    append_shadow_passes_if_needed(graph, layer_output, item, casters, ctx, node_ctx);
    append_depth_grade_pass_if_needed(graph, layer_output, item, ctx, depth_grade, node_ctx);
    append_effect_pass_if_needed(graph, layer_output, *item.layer, item, cam25d, ctx, node_ctx);

    if (layer.track_matte.active() && item.matte_node != k_invalid_node) {
        cache::NodeCacheKey matte_key{
            .scope       = "matte:" + std::string(layer.name),
            .frame       = (layer.cache_static || item.is_static) ? Frame{0} : ctx.frame_input.frame,
            .width       = ctx.frame_input.width,
            .height      = ctx.frame_input.height,
            .params_hash = hash_bytes(layer.track_matte.source_layer.data(),
                                      layer.track_matte.source_layer.size()),
        };
        matte_key.params_hash = hash_combine(
            matte_key.params_hash,
            static_cast<u64>(layer.track_matte.type));

        // TICKET-ae-cam-hash-collision Soluzione B — track-matte cache keys
        // also participate in the framebuffer cache lookup chain, so they
        // MUST differentiate per-camera-state (track-matte compositing is
        // camera-position relative on multi-camera compositions).
        if (ctx.frame_input.has_camera_2_5d) {
            cache::fold_camera_into_params_hash(matte_key, ctx.frame_input.camera_2_5d);
        }

        // PR2-cleanup: TrackMatteNode carries its policy in `m_cache_policy` (ctor-time).
        {
            GraphNodeId matte_node = graph.add_node(std::make_unique<TrackMatteNode>(
                layer.track_matte.type, std::string(layer.name), matte_key), node_ctx);
            graph.connect(layer_output, matte_node);
            graph.connect(item.matte_node, matte_node);
            layer_output = matte_node;
        }
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
            // PR2-cleanup: TransitionNode is intrinsically frame-variant via its ctor.
            {
                GraphNodeId trans_node = graph.add_node(std::make_unique<TransitionNode>(
                    std::string(layer.name), active_spec, is_out, layer.from, layer.duration), node_ctx);
                graph.connect(layer_output, trans_node);
                layer_output = trans_node;
            }
        }
    }

    append_composite_pass(graph, current, layer_output, *item.layer, (layer.cache_static || item.is_static), ctx, item.world_z, node_ctx);
}

void sort_camera25d_layers(std::vector<LayerGraphItem>& items) {
    std::stable_sort(items.begin(), items.end(),
        [](const LayerGraphItem& a, const LayerGraphItem& b) {
            if (a.depth != b.depth) return a.depth > b.depth;
            return a.insertion_index < b.insertion_index;
        });
}

} // namespace chronon3d::graph::detail
