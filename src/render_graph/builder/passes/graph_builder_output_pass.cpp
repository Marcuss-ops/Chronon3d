#include "graph_builder_passes.hpp"
#include "../graph_builder_pipeline.hpp"
#include <chronon3d/render_graph/builder/graph_build_context.hpp>
#include <chronon3d/render_graph/nodes/per_pixel_dof_node.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <spdlog/spdlog.h>
#include <queue>
#include <unordered_set>

namespace chronon3d::graph::detail {

// ── is_full_frame_opaque (moved from graph_builder_pipeline.cpp) ──────
// Checks if a node and its entire subtree produce a full-frame opaque output.
// Used by the early-exit analysis to skip background subtrees.

static bool is_full_frame_opaque_impl(
    GraphNodeId id,
    const RenderGraph& graph,
    const RenderGraphContext& ctx,
    std::unordered_map<GraphNodeId, bool>& memo)
{
    auto it = memo.find(id);
    if (it != memo.end()) return it->second;

    const auto& node = graph.node(id);
    bool result = false;

    if (node.kind() == RenderGraphNodeKind::Source) {
        const auto* multi = dynamic_cast<const MultiSourceNode*>(&node);
        if (multi && multi->is_single_full_frame_image()) {
            result = true;
        } else {
            result = node.can_seed_full_frame(ctx);
        }
    } else if (node.kind() == RenderGraphNodeKind::Transform) {
        const auto* transform = dynamic_cast<const TransformNode*>(&node);
        if (transform) {
            if (transform->is_identity() && transform->opacity() >= 0.999f) {
                const auto& inputs = graph.inputs(id);
                if (!inputs.empty()) {
                    result = is_full_frame_opaque_impl(inputs[0], graph, ctx, memo);
                }
            }
        }
    } else if (node.kind() == RenderGraphNodeKind::Effect) {
        const auto* effect = dynamic_cast<const EffectStackNode*>(&node);
        if (effect && effect->is_color_only()) {
            const auto& inputs = graph.inputs(id);
            if (!inputs.empty()) {
                result = is_full_frame_opaque_impl(inputs[0], graph, ctx, memo);
            }
        }
    } else if (node.kind() == RenderGraphNodeKind::Adjustment) {
        const auto* adj = dynamic_cast<const AdjustmentNode*>(&node);
        if (adj && adj->is_color_only()) {
            const auto& inputs = graph.inputs(id);
            if (!inputs.empty()) {
                result = is_full_frame_opaque_impl(inputs[0], graph, ctx, memo);
            }
        }
    } else if (node.kind() == RenderGraphNodeKind::Composite) {
        const auto* comp = dynamic_cast<const CompositeNode*>(&node);
        if (comp && comp->blend_mode() == BlendMode::Normal) {
            const auto& inputs = graph.inputs(id);
            if (inputs.size() == 2) {
                result = is_full_frame_opaque_impl(inputs[1], graph, ctx, memo);
            }
        }
    }

    memo[id] = result;
    return result;
}

// ── OutputPass ────────────────────────────────────────────────────────

void OutputPass::run(GraphBuildContext& ctx) {
    auto& graph = *ctx.graph;
    auto& rctx = *ctx.render_ctx;
    const auto& cam25d = ctx.cam25d;
    GraphNodeId current = ctx.current_output;

    // ── Per-pixel DOF post-composite pass ──────────────────────────────
    // When the 2.5D camera has DOF enabled, insert a PerPixelDofNode after
    // all layers are composited.
    if (cam25d.dof.enabled && cam25d.enabled) {
        rctx.options.track_dof_depth = true;
        rctx.telemetry.dof_depth.assign(
            static_cast<size_t>(rctx.frame.width) * rctx.frame.height,
            1e18f  // sentinel: no layer contributed
        );
        auto dof_node = graph.add_node(PerPixelDofNode::create(cam25d));
        // PerPixelDofNode ctor already sets frame_variant_cache("per_pixel_dof");
        // builder override is redundant because DOF is inherently per-frame.
        graph.connect(current, dof_node);
        current = dof_node;
    }

    graph.set_output(current);

    // ── Early-exit analysis ────────────────────────────────────────────
    // Mark nodes covered by full-frame opaque layers for skip during execution.
    rctx.tile.early_exit_skip.assign(graph.size(), false);
    {
        std::unordered_map<GraphNodeId, bool> opaque_memo;
        std::vector<GraphNodeId> stack;
        if (graph.has_output()) {
            stack.push_back(graph.output());
        }

        while (!stack.empty()) {
            GraphNodeId id = stack.back();
            stack.pop_back();

            if (id >= static_cast<GraphNodeId>(graph.size())) continue;
            if (rctx.tile.early_exit_skip[id]) continue;

            const auto& node = graph.node(id);
            if (node.kind() == RenderGraphNodeKind::Composite) {
                const auto& inputs = graph.inputs(id);
                if (inputs.size() == 2) {
                    GraphNodeId bg_id    = inputs[0];
                    GraphNodeId layer_id = inputs[1];

                    bool layer_fully_covers = is_full_frame_opaque_impl(
                        layer_id, graph, rctx, opaque_memo);

                    if (rctx.options.diagnostics_enabled) {
                        spdlog::info(
                            "[early-exit-debug] composite_id={} layer_id={} "
                            "layer_name='{}' kind={} layer_fully_covers={}",
                            id, layer_id, graph.node(layer_id).name(),
                            to_string(graph.node(layer_id).kind()),
                            layer_fully_covers ? 1 : 0);
                    }

                    if (layer_fully_covers) {
                        // Mark the entire background subtree for skip
                        std::queue<GraphNodeId> bg_queue;
                        std::unordered_set<GraphNodeId> visited;
                        bg_queue.push(bg_id);
                        while (!bg_queue.empty()) {
                            GraphNodeId bg = bg_queue.front();
                            bg_queue.pop();
                            if (visited.count(bg)) continue;
                            if (bg >= static_cast<GraphNodeId>(graph.size())) continue;
                            visited.insert(bg);
                            rctx.tile.early_exit_skip[bg] = true;
                            for (GraphNodeId bg_in : graph.inputs(bg)) {
                                bg_queue.push(bg_in);
                            }
                        }
                    } else {
                        for (GraphNodeId in : graph.inputs(id)) {
                            stack.push_back(in);
                        }
                    }
                }
            } else {
                for (GraphNodeId in : graph.inputs(id)) {
                    stack.push_back(in);
                }
            }
        }
    }

    ctx.current_output = current;
}

// ── is_full_frame_opaque (public detail function) ─────────────────────

bool is_full_frame_opaque(
    GraphNodeId id,
    const RenderGraph& graph,
    const RenderGraphContext& ctx,
    std::unordered_map<GraphNodeId, bool>& memo)
{
    return is_full_frame_opaque_impl(id, graph, ctx, memo);
}

} // namespace chronon3d::graph::detail
