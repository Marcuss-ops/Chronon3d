#include "graph_builder_layer_passes.hpp"
#include "../graph_builder_coordinates.hpp"
#include "../evaluated_layer_placement.hpp"

#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <memory>
#include <atomic>
#include <cstdint>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

// ── composite pass ────────────────────────────────────────────────

void append_composite_pass(RenderGraph& graph, GraphNodeId& current,
                           GraphNodeId layer_output, const Layer& layer,
                           bool is_static, const RenderGraphContext& ctx,
                           float world_z,
                           const BuilderContext& node_ctx) {
    if (layer_output == k_invalid_node || layer_output == current) {
        if (ctx.policy.diagnostics_enabled) {
            spdlog::info("[append_composite_pass] skipped layer='{}' layer_output={} current={}", layer.name.c_str(), layer_output, current);
        }
        return;
    }

    if (!ctx.policy.dirty_rects_enabled &&
        current < graph.size() &&
        graph.node(current).kind() == RenderGraphNodeKind::Output &&
        graph.node(current).name() == "Clear" &&
        layer.blend_mode == chronon3d::BlendMode::Normal &&
        graph.node(layer_output).can_seed_full_frame(ctx)) {
        if (ctx.node_exec.counters) {
            const auto pixels = static_cast<std::uint64_t>(ctx.frame_input.width) *
                static_cast<std::uint64_t>(ctx.frame_input.height);
            ctx.node_exec.counters->clear_skipped_calls.fetch_add(
                1, std::memory_order_relaxed);
            ctx.node_exec.counters->clear_skipped_pixels.fetch_add(
                pixels, std::memory_order_relaxed);
        }
        current = layer_output;
        return;
    }

auto composite = graph.add_node(std::make_unique<CompositeNode>(
    graph.next_composite_id(),
    layer.blend_mode,
    is_static ? Frame{0} : Frame{-1},
    world_z,
    ::chronon3d::CompositeOperator::SourceOver,
    is_static ? static_memory_cache("composite") : frame_variant_cache("composite")
), node_ctx);
    graph.connect(current, composite);
    graph.connect(layer_output, composite);
    current = composite;
}

// ── effect pass ───────────────────────────────────────────────────

void append_effect_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                  const Layer& layer, const LayerGraphItem& item,
                                  const Camera2_5DRuntime& cam25d,
                                  const RenderGraphContext& ctx,
                                  const BuilderContext& node_ctx) {
    const bool is_static = layer.cache_static || item.is_static;

    // Layer effects — the EffectCatalog factory bakes cache policy into
    // the constructed node; in-place mutation was removed. Single id.
    for (const auto& effect : layer.effects()) {
        if (!effect.enabled) continue;
        const auto* ec = ctx.services.effect_catalog;
        GraphNodeId effect_id = graph.add_node(ec->create_node(effect), node_ctx);
        graph.connect(layer_output, effect_id);
        layer_output = effect_id;
    }

    // DOF blur (only for projected 2.5D layers).
    // The camera DOF path is scene-level and is inserted by OutputPass as a
    // single PerPixelDofNode after compositing. Do not consult
    // ctx.policy.track_dof_depth here: OutputPass sets that policy after the
    // layer graph is built, so the old check caused one legacy DofEffectNode
    // per projected layer plus the final post-composite node (double DOF).
    if (item.projected && cam25d.dof.enabled) {
        // Intentionally no per-layer node: the final post-composite pass owns
        // the camera DOF contract and has the complete depth/alpha field.
    }
}

// ── mask pass ─────────────────────────────────────────────────────

void append_mask_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                const LayerGraphItem& item,
                                const RenderGraphContext& ctx,
                                const BuilderContext& node_ctx) {
    const Layer& layer = *item.layer;
    if (!layer.mask.enabled()) return;

    Mask mask = layer.mask;
    if (ctx.policy.modular_coordinates && !item.native_3d && !item.projected) {
        // Mask coordinates are authored in the layer's centered local space,
        // while MaskNode samples canvas pixels.  Convert the resolved layer
        // translation back to the centered origin before rasterizing it.
        mask.pos.x += item.transform.position.x - ctx.frame_input.width * 0.5f;
        mask.pos.y += item.transform.position.y - ctx.frame_input.height * 0.5f;
    }

    const bool is_static = layer.cache_static || item.is_static;
    // PR2-cleanup: MaskNode defaults to `static_memory_cache`; the legacy
    // `!is_static` A/B distinction was removed. Single node, taken as id.
    {
        GraphNodeId masked = graph.add_node(std::make_unique<MaskNode>(std::move(mask), is_static ? Frame{0} : Frame{-1}), node_ctx);
        graph.connect(layer_output, masked);
        layer_output = masked;
    }
}

// ── transform pass ────────────────────────────────────────────────

void append_transform_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                     const LayerGraphItem& item, const RenderGraphContext& ctx,
                                     const BuilderContext& node_ctx) {
    const Layer& layer = *item.layer;

    const bool needs_transform = layer_needs_render_transform(item, ctx);

    if (!needs_transform) return;

    std::unique_ptr<TransformNode> transform_node;
    // A projected layer may be authoring-static while its camera-space
    // transform changes every frame. Persisting that transformed framebuffer
    // in the inter-frame cache creates one large entry per camera pose and
    // defeats the framebuffer lifetime/aliasing path. Tight text raster is
    // cached separately; the camera projection is deliberately transient.
    const bool is_static = (layer.cache_static || item.is_static) && !item.projected;
    const Frame cache_frame = is_static ? Frame{0} : Frame{-1};
    const auto placement = evaluate_layer_placement(item, ctx);
    transform_node = std::make_unique<TransformNode>(placement.render_matrix,
                                                     placement.opacity,
                                                     cache_frame,
                                                     SamplingMode::Bilinear,
                                                     is_static ? static_memory_cache("transform") : frame_variant_cache("transform"),
                                                     placement.surface_size);

    // PR2-cleanup: TransformNode carries its policy in `m_cache_policy` (ctor-time).
    {
        GraphNodeId transform = graph.add_node(std::move(transform_node), node_ctx);
        graph.connect(layer_output, transform);
        layer_output = transform;
    }
}

} // namespace chronon3d::graph::detail
