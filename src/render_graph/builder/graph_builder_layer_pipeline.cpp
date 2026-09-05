#include "graph_builder_pipeline.hpp"

#include <chronon3d/cache/node_cache_identity_builder.hpp>
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
#include "evaluated_layer_placement.hpp"

namespace chronon3d::graph::detail {

GraphNodeId append_root_sources(RenderGraph& graph, const Scene& scene,
                                RenderGraphContext& ctx,
                                GraphNodeId current) {
    bool first_root_source = true;
    for (const auto& node : scene.nodes()) {
        // Canonical cache identity (TICKET-ae-cam-hash-collision Soluzione
        // B): the builder folds the camera fingerprint by construction so a
        // cinematic camera-driven composition with mixed root/child sources
        // stays deterministic.
        cache::NodeCacheKey source_key = cache::NodeCacheIdentityBuilder{
            "root.source:" + std::string(node.name)
        }
            .frame(ctx.frame_input.frame)
            .output(ctx.frame_input.width, ctx.frame_input.height)
            .params(hash_render_node(node))
            .source(hash_bytes(node.name.data(), node.name.size()))
            .camera_if(ctx.frame_input.has_camera_2_5d,
                       ctx.frame_input.camera_2_5d)
            .build();

        // Root nodes already carry top-left pixel placement in their
        // world transform. The canonical root-source resolver owns the
        // only compatibility bake retained for modular line sources.
        const auto root_matrix = root_source_matrix_override(node, ctx);
        auto source = graph.add_node(std::make_unique<SourceNode>(
            std::string(node.name), node, source_key,
            root_matrix,
            std::optional<f32>(node.world_transform.opacity),
            // Root nodes are evaluated from the current Scene, and the
            // authored root payload may change between frames. They do not
            // have the layer static-analysis metadata used by layer sources,
            // so the conservative correct policy is frame-variant.
            frame_variant_cache("root_source")
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

GraphNodeId build_layer_output_node(
    RenderGraph& graph, const LayerGraphItem& item,
    RenderGraphContext& ctx,
    const Camera2_5DRuntime& cam25d,
    std::span<const ShadowCasterInfo> casters,
    const rendering::DepthGrade& depth_grade,
    const BuilderContext& node_ctx)
{
    GraphNodeId layer_output = append_source_pass(graph, item, ctx, node_ctx);
    const Layer& layer = *item.layer;

    if (!ctx.policy.skip_initial_clear && layer_output != k_invalid_node) {
        const bool simple_opaque_full_frame_layer =
            (layer.kind == LayerKind::Normal || layer.kind == LayerKind::Shape || layer.kind == LayerKind::Text || layer.kind == LayerKind::Video) &&
            layer.nodes.size() == 1 &&
            layer.mask.type == MaskType::None &&
            layer.effects().empty() &&
            layer.blend_mode == BlendMode::Normal &&
            !layer.track_matte.active() &&
            (layer.transition_in.transition_id.empty() || layer.transition_in.transition_id == "none") &&
            (layer.transition_out.transition_id.empty() || layer.transition_out.transition_id == "none");

        const bool can_seed = graph.node(layer_output).can_seed_full_frame(ctx);

        if (simple_opaque_full_frame_layer && can_seed) {
            ctx.policy.skip_initial_clear = true;
        }
    }

    if (layer.kind == LayerKind::Adjustment) {
        // Adjustment layers are a scene-level effect applied to the current
        // composite, not a layer output. They should not be built here.
        return k_invalid_node;
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
        // Canonical cache identity (TICKET-ae-cam-hash-collision Soluzione
        // B): track-matte keys participate in the framebuffer cache lookup
        // chain and MUST differentiate per-camera-state (track-matte
        // compositing is camera-position relative on multi-camera comps).
        cache::NodeCacheKey matte_key = cache::NodeCacheIdentityBuilder{
            "matte:" + std::string(layer.name)
        }
            .frame((layer.cache_static || item.is_static) ? Frame{0}
                                                          : ctx.frame_input.frame)
            .output(ctx.frame_input.width, ctx.frame_input.height)
            .params(hash_combine(
                hash_bytes(layer.track_matte.source_layer.data(),
                           layer.track_matte.source_layer.size()),
                static_cast<u64>(layer.track_matte.type)))
            .camera_if(ctx.frame_input.has_camera_2_5d,
                       ctx.frame_input.camera_2_5d)
            .build();

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

    // Apply transition-in first, then transition-out.  Both can coexist;
    // each TransitionNode evaluates its own progress window, so the
    // inactive one is identity (p = 0 / p = 1) while the other runs.
    if (has_in_trans) {
        GraphNodeId trans_node = graph.add_node(std::make_unique<TransitionNode>(
            std::string(layer.name), layer.transition_in, false, layer.from, layer.duration), node_ctx);
        graph.connect(layer_output, trans_node);
        layer_output = trans_node;
    }

    if (has_out_trans) {
        GraphNodeId trans_node = graph.add_node(std::make_unique<TransitionNode>(
            std::string(layer.name), layer.transition_out, true, layer.from, layer.duration), node_ctx);
        graph.connect(layer_output, trans_node);
        layer_output = trans_node;
    }

    return layer_output;
}

void append_layer_pipeline(RenderGraph& graph, const LayerGraphItem& item,
                           GraphNodeId& current, RenderGraphContext& ctx,
                           const Camera2_5DRuntime& cam25d,
                           std::span<const ShadowCasterInfo> casters,
                           const rendering::DepthGrade& depth_grade,
                           const GraphBuildContext*) {
    BuilderContext node_ctx{
        .layer_id = std::string(item.layer->name),
        .opacity_evaluator = [opacity = item.layer->anim_transform.opacity](const RenderFrameInfo& info) -> float {
            return opacity.evaluate(info.sample_time);
        },
        .layer_index = static_cast<std::uint32_t>(item.insertion_index),
        .item_index = 0,
    };

    const Layer& layer = *item.layer;

    // Projection/frustum/backface resolution is authoritative.  A projected
    // layer can remain structurally present in the graph while being marked
    // invisible by resolve_layer_graph_item(); do not build or composite its
    // source in that case.  Without this guard BackfaceMode::Hidden emitted
    // the correct diagnostic but the stale source still reached the output.
    if (ctx.policy.diagnostics_enabled) {
        spdlog::info("[append_layer_pipe] frame={} layer='{}' item_vis={} layer_vis={} anim_op_td={} opacity={}",
                     static_cast<int>(ctx.frame_input.frame), layer.name.c_str(),
                     item.visible, layer.visible,
                     layer.anim_transform.opacity.is_time_dependent(),
                     layer.transform.opacity);
    }
    if (!item.visible || !layer.visible) {
        return;
    }

    // A fully transparent layer is identity for SourceOver composition. Do
    // not build a transparent source/composite branch: besides wasting the
    // raster pass, it can replace a valid backdrop when an opaque fast path
    // sees stale bounds from the skipped content.
    //
    // Only a STATICALLY transparent layer is structural and safe to cull at
    // topology build time.  `transform.opacity` is the value baked by
    // LayerBuilder::build() at the current frame, so for an ANIMATED opacity
    // (fade-in/slide-in) it is 0.0 at frame 0 and rises later.  Culling on
    // that baked value would freeze the frame-0 decision into the cached
    // graph topology and drop the layer from every subsequent frame of a
    // range/video render (the topology is reused via the structure
    // fingerprint).  Animated layers stay in the graph; their per-frame
    // opacity is re-evaluated by the refresh path / node opacity evaluator.
    if (!layer.anim_transform.opacity.is_time_dependent() &&
        layer.transform.opacity <= 0.0f) {
        return;
    }

    // Adjustment layers are a scene-level effect applied to the current
    // composite, not a layer output. Handle them before the normal
    // layer pipeline.
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

    GraphNodeId layer_output = build_layer_output_node(
        graph, item, ctx, cam25d, casters, depth_grade, node_ctx);
    if (ctx.policy.diagnostics_enabled) {
        spdlog::info("[append_layer_pipe] layer='{}' layer_output={} current={}",
                     layer.name.c_str(), layer_output, current);
    }

    const bool is_static = layer.cache_static || item.is_static;
    append_composite_pass(graph, current, layer_output, layer, is_static, ctx, item.world_z, node_ctx);
    if (ctx.policy.diagnostics_enabled) {
        spdlog::info("[append_layer_pipe] after composite current={}", current);
    }
}

void sort_camera25d_layers(std::vector<LayerGraphItem>& items) {
    std::stable_sort(items.begin(), items.end(),
        [](const LayerGraphItem& a, const LayerGraphItem& b) {
            if (a.depth != b.depth) return a.depth > b.depth;
            return a.insertion_index < b.insertion_index;
        });
}

} // namespace chronon3d::graph::detail
