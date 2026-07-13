#include "graph_builder_layer_passes.hpp"
#include "../graph_builder_coordinates.hpp"

#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/dof_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <memory>

namespace chronon3d::graph::detail {

using namespace chronon3d::graph;

// ── composite pass ────────────────────────────────────────────────

void append_composite_pass(RenderGraph& graph, GraphNodeId& current,
                           GraphNodeId layer_output, const Layer& layer,
                           bool is_static, const RenderGraphContext& ctx,
                           float world_z) {
    if (layer_output == k_invalid_node || layer_output == current) return;

    if (!ctx.policy.dirty_rects_enabled &&
        current < graph.size() &&
        graph.node(current).kind() == RenderGraphNodeKind::Output &&
        graph.node(current).name() == "Clear" &&
        layer.blend_mode == chronon3d::BlendMode::Normal &&
        graph.node(layer_output).can_seed_full_frame(ctx)) {
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
));
    graph.connect(current, composite);
    graph.connect(layer_output, composite);
    current = composite;
}

// ── effect pass ───────────────────────────────────────────────────

void append_effect_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                  const Layer& layer, const LayerGraphItem& item,
                                  const Camera2_5DRuntime& cam25d,
                                  const RenderGraphContext& ctx) {
    const bool is_static = layer.cache_static || item.is_static;

    // Layer effects — the EffectCatalog factory bakes cache policy into
    // the constructed node; in-place mutation was removed. Single id.
    for (const auto& effect : layer.effects()) {
        if (!effect.enabled) continue;
        const auto* ec = ctx.services.effect_catalog;
        GraphNodeId effect_id = graph.add_node(ec->create_node(effect));
        graph.connect(layer_output, effect_id);
        layer_output = effect_id;
    }

    // DOF blur (only for projected 2.5D layers)
    // Skip per-layer DOF when scene-level per-pixel DOF is active —
    // the PerPixelDofNode handles all DOF after compositing.
    if (item.projected && cam25d.dof.enabled) {
        // Per-pixel DOF is signalled by track_dof_depth being set in the ctx.
        // When active, the per-layer DofEffectNode is skipped to avoid
        // double-blurring.
        if (!ctx.policy.track_dof_depth) {
            // PR2-cleanup: DofEffectNode is intrinsically frame-dependent via its
            // built-in `static_memory_cache` default; the legacy `!is_static`
            // distinction was removed. Single node, taken as id.
            {
                GraphNodeId dof_node = graph.add_node(DofEffectNode::create(cam25d, item.world_z));
                graph.connect(layer_output, dof_node);
                layer_output = dof_node;
            }
        }
    }
}

// ── mask pass ─────────────────────────────────────────────────────

void append_mask_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                const LayerGraphItem& item,
                                const RenderGraphContext& ctx) {
    const Layer& layer = *item.layer;
    if (!layer.mask.enabled()) return;

    const bool is_static = layer.cache_static || item.is_static;
    // PR2-cleanup: MaskNode defaults to `static_memory_cache`; the legacy
    // `!is_static` A/B distinction was removed. Single node, taken as id.
    {
        GraphNodeId masked = graph.add_node(std::make_unique<MaskNode>(layer.mask, is_static ? Frame{0} : Frame{-1}));
        graph.connect(layer_output, masked);
        layer_output = masked;
    }
}

// ── transform pass ────────────────────────────────────────────────

void append_transform_pass_if_needed(RenderGraph& graph, GraphNodeId& layer_output,
                                     const LayerGraphItem& item, const RenderGraphContext& ctx) {
    const Layer& layer = *item.layer;

    const bool needs_transform = layer_needs_render_transform(item, ctx);

    if (!needs_transform) return;

    std::unique_ptr<TransformNode> transform_node;
    const bool is_static = layer.cache_static || item.is_static;
    const Frame cache_frame = is_static ? Frame{0} : Frame{-1};
    if (item.projected) {
        transform_node = std::make_unique<TransformNode>(item.projection_matrix,
                                                         layer.transform.opacity,
                                                         cache_frame,
                                                         SamplingMode::Bilinear,
                                                         is_static ? static_memory_cache("transform") : frame_variant_cache("transform"));
    } else {
        Mat4 effective_matrix = item.world_matrix;
        if (should_use_centered_rendering(item, ctx)) {
            Mat4 ssaa_world = item.world_matrix;
            ssaa_world[3][0] *= ctx.policy.ssaa_factor;
            ssaa_world[3][1] *= ctx.policy.ssaa_factor;
            ssaa_world[3][2] *= ctx.policy.ssaa_factor;
            effective_matrix =
                glm::translate(Mat4(1.0f), Vec3(-ctx.frame_input.width * 0.5f, -ctx.frame_input.height * 0.5f, 0.0f)) *
                ssaa_world;
        } else {
            // Delegate to the shared helper so the build-path stays in sync
            // with the refresh-path (scene_refresh.hpp) — both now strip the
            // implicit canvas-center translation for non-3D Normal layers,
            // preventing the double-centring bug.
            effective_matrix = strip_implicit_canvas_centering(effective_matrix, item, ctx);
        }
        transform_node = std::make_unique<TransformNode>(effective_matrix,
                                                         item.transform.opacity,
                                                         cache_frame,
                                                         SamplingMode::Bilinear,
                                                         is_static ? static_memory_cache("transform") : frame_variant_cache("transform"));
    }

    // PR2-cleanup: TransformNode carries its policy in `m_cache_policy` (ctor-time).
    {
        GraphNodeId transform = graph.add_node(std::move(transform_node));
        graph.connect(layer_output, transform);
        layer_output = transform;
    }
}

} // namespace chronon3d::graph::detail
