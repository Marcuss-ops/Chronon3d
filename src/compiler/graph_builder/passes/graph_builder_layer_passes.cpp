#include "graph_builder_layer_passes.hpp"
#include "graph_builder_coordinates.hpp"
#include "evaluated_layer_placement.hpp"

#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <algorithm>
#include <memory>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <spdlog/spdlog.h>

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

auto composite_node = std::make_unique<CompositeNode>(
    graph.next_composite_id(),
    layer.blend_mode,
    is_static ? Frame{0} : Frame{-1},
    world_z,
    ::chronon3d::CompositeOperator::SourceOver,
    is_static ? static_memory_cache("composite") : frame_variant_cache("composite")
);
composite_node->set_backdrop_blur(layer.backdrop_blur);
auto composite = graph.add_node(std::move(composite_node), node_ctx);
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
    std::size_t effect_index = 0;
    for (const auto& effect : layer.effects()) {
        const auto current_effect_index = effect_index++;
        if (!effect.enabled) continue;
        const auto* ec = ctx.build_dependencies
            ? ctx.build_dependencies->effect_catalog : nullptr;
        if (!ec) throw std::logic_error("effect catalog not wired for graph build");
        auto effect_node = ec->create_node(effect);
        if (effect.effect_type == effects::EffectType::Echo) {
            if (auto* stack = dynamic_cast<EffectStackNode*>(effect_node.get()))
                stack->set_temporal_history_key(
                    std::string(layer.name) + "/echo/" + std::to_string(current_effect_index));
        }
        GraphNodeId effect_id = graph.add_node(std::move(effect_node), node_ctx);
        graph.connect(layer_output, effect_id);
        if(effect.effect_type==effects::EffectType::DisplacementMap) {
            const auto* params=std::get_if<DisplacementMapParams>(&effect.params);
            if(!params) throw std::logic_error("DisplacementMap effect has malformed params");
            if (params->field) { layer_output = effect_id; continue; }
            const auto map=item.displacement_map_nodes.find(params->map_layer);
            if(map==item.displacement_map_nodes.end())
                throw std::runtime_error("DisplacementMap source layer was not materialized: "+params->map_layer);
            graph.connect(map->second,effect_id);
        }
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
    std::vector<std::string> text_sources;
    const auto add_text_source = [&](MaskType type, const std::string& source) {
        if (type == MaskType::Text &&
            std::find(text_sources.begin(), text_sources.end(), source) == text_sources.end())
            text_sources.push_back(source);
    };
    add_text_source(mask.type, mask.text_source);
    for (const auto& operation : mask.operations)
        add_text_source(operation.type, operation.text_source);
    const bool video_source_mask = layer.kind == LayerKind::Video;
    if (ctx.policy.modular_coordinates && !item.native_3d && !item.projected &&
        !video_source_mask) {
        // Shape masks are authored in the resolved layer coordinate space,
        // while MaskNode samples canvas pixels. Convert their layer
        // translation back to the centered origin before rasterizing them.
        // Video masks run on the decoded source surface before TransformNode;
        // they already use coordinates centered on that local surface.
        const Vec3 offset{
            item.transform.position.x - ctx.frame_input.width * 0.5f,
            item.transform.position.y - ctx.frame_input.height * 0.5f,
            0.0f};
        mask.pos += offset;
        for (auto& operation : mask.operations) operation.pos += offset;
    }

    const bool is_static = layer.cache_static || item.is_static;
    // PR2-cleanup: MaskNode defaults to `static_memory_cache`; the legacy
    // `!is_static` A/B distinction was removed. Single node, taken as id.
    {
        GraphNodeId masked = graph.add_node(std::make_unique<MaskNode>(
            std::move(mask), is_static && text_sources.empty()
                ? Frame{0} : Frame{-1}, text_sources,
            evaluate_layer_placement(item, ctx).render_matrix,
            text_sources.empty() ? static_memory_cache("mask")
                                 : frame_variant_cache("text_mask_source"),
            video_source_mask), node_ctx);
        graph.connect(layer_output, masked);
        for (const auto& source : text_sources) {
            const auto node = item.text_mask_source_nodes.find(source);
            if (node == item.text_mask_source_nodes.end())
                throw std::runtime_error("text mask source layer was not materialized: " + source);
            graph.connect(node->second, masked);
        }
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
    // transform changes every frame. Persisting that transformed surface
    // in the inter-frame cache creates one large entry per camera pose and
    // defeats the surface lifetime/aliasing path. Tight text raster is
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
