#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>

#include "refresh/source.hpp"
#include "refresh/multi_source.hpp"
#include "refresh/effect_stack.hpp"
#include "refresh/transform.hpp"
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include "../builder/graph_builder_coordinates.hpp"
#include "refresh/layer_item.hpp"
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

#include "../builder/graph_builder_pipeline.hpp"
#include <chronon3d/scene/model/core/scene.hpp>
#include <string>
#include <unordered_map>

namespace chronon3d::graph::detail {

void refresh_compiled_graph_payloads(
    CompiledFrameGraph& compiled,
    const Scene& scene,
    RenderGraphContext& ctx,
    const LayerResolutionResult& resolved)
{
    // ── 1. Compute recursive static analysis ─────────────────────────────
    // Must match the builder path (graph_builder_source_pass.cpp) which uses
    // item.is_static from this cache.  Without this, source_is_static uses
    // only layer.cache_static, missing parent/transition/animated propagation.
    std::unordered_map<std::string, bool> is_static_cache;
    compute_static_layers(resolved, is_static_cache);

    // ── 2. Build resolved layer lookup (by name) ─────────────────────────
    std::unordered_map<std::string, const ResolvedLayer*> resolved_by_name;
    resolved_by_name.reserve(resolved.layers.size());
    for (const auto& rl : resolved.layers) {
        if (rl.layer) {
            resolved_by_name.emplace(std::string(rl.layer->name), &rl);
        }
    }

    // ── 3. Build scene root node lookup (by name) ────────────────────────
    std::unordered_map<std::string, const RenderNode*> root_nodes_by_name;
    root_nodes_by_name.reserve(scene.nodes().size());
    for (const auto& node : scene.nodes()) {
        root_nodes_by_name.emplace(std::string(node.name), &node);
    }

    // ── 4. Iterate compiled graph nodes and dispatch refreshers ───────────
    for (size_t id = 0; id < compiled.graph.size(); ++id) {
        if (!compiled.graph.has_node(static_cast<GraphNodeId>(id))) {
            continue;
        }

        auto& graph_node = compiled.graph.node(static_cast<GraphNodeId>(id));

        // Dispatch via kind() to avoid sequential dynamic_cast RTTI lookups.
        // SourceNode and MultiSourceNode both report Source kind, so the
        // Source case still needs a single dynamic_cast to disambiguate.
        switch (graph_node.kind()) {
            case RenderGraphNodeKind::Source:
                if (auto* source_node = dynamic_cast<SourceNode*>(&graph_node)) {
                    refresh_source_node(*source_node, resolved_by_name,
                        root_nodes_by_name, is_static_cache, ctx);
                } else if (auto* multi = dynamic_cast<MultiSourceNode*>(&graph_node)) {
                    refresh_multi_source_node(*multi, resolved_by_name,
                        is_static_cache, ctx);
                }
                break;
            case RenderGraphNodeKind::Effect:
                refresh_effect_stack_node(
                    static_cast<EffectStackNode&>(graph_node),
                    resolved_by_name, ctx);
                break;
            case RenderGraphNodeKind::Transform:
                refresh_transform_node(
                    static_cast<TransformNode&>(graph_node),
                    resolved_by_name, ctx);
                break;
            case RenderGraphNodeKind::TextRun: {
                auto& text = static_cast<TextRunNode&>(graph_node);
                for (const auto& [name, rl] : resolved_by_name) {
                    if (!rl || !rl->layer || rl->layer->kind != LayerKind::Text
                        || rl->layer->nodes.size() != 1
                        || rl->layer->nodes[0].name != text.name()) {
                        continue;
                    }
                    const auto& render_ref = rl->layer->nodes[0];
                    const auto item = make_layer_graph_item_for_refresh(*rl, ctx);
                    f32 opacity = 1.0f;
                    const auto placement = resolve_text_run_placement(
                        item, render_ref, ctx, opacity);
                    cache::NodeCacheKey key{
                        .scope = "layer.textrun:" + name + ":"
                            + std::string(render_ref.name),
                        .frame = ctx.frame_input.frame,
                        .width = ctx.frame_input.width,
                        .height = ctx.frame_input.height,
                        .params_hash = hash_render_node_content_only(render_ref),
                        .source_hash = hash_combine(
                            hash_string(render_ref.name),
                            hash_render_node_placement_only(render_ref))
                    };
                    text.refresh_placement(
                        render_ref, placement, key,
                        std::optional<f32>(opacity));
                    break;
                }
                break;
            }
            default:
                break;
        }
    }
}

} // namespace chronon3d::graph::detail
