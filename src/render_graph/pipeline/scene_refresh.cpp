#include "scene_refresh.hpp"

#include "refresh/source.hpp"
#include "refresh/multi_source.hpp"
#include "refresh/effect_stack.hpp"
#include "refresh/transform.hpp"

#include "../builder/graph_builder_internal.hpp"
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
            default:
                break;
        }
    }
}

} // namespace chronon3d::graph::detail
