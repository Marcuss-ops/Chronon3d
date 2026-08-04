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

namespace {

SceneRefreshResult topology_mismatch(std::string message) {
    return SceneRefreshResult{SceneRefreshStatus::TopologyMismatch, std::move(message)};
}

SceneRefreshResult missing_dynamic_data(std::string message) {
    return SceneRefreshResult{SceneRefreshStatus::MissingDynamicData, std::move(message)};
}

const Layer* find_layer(const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name,
                       std::string_view name) {
    const auto it = resolved_by_name.find(std::string{name});
    return it == resolved_by_name.end() || !it->second ? nullptr : it->second->layer;
}

SceneRefreshResult validate_compiled_structure(
    const CompiledFrameGraph& compiled,
    const Scene& scene,
    const std::unordered_map<std::string, const ResolvedLayer*>& resolved_by_name)
{
    if (compiled.nodes.size() < compiled.graph.size()) {
        return topology_mismatch("compiled node metadata is shorter than the graph");
    }

    for (size_t id = 0; id < compiled.graph.size(); ++id) {
        if (!compiled.graph.has_node(static_cast<GraphNodeId>(id))) continue;
        const auto& node = compiled.graph.node(static_cast<GraphNodeId>(id));
        const auto& info = compiled.nodes[id];
        if (info.id != static_cast<GraphNodeId>(id) || info.kind != node.kind() ||
            info.inputs != compiled.graph.inputs(static_cast<GraphNodeId>(id))) {
            return topology_mismatch("compiled graph metadata does not match node topology at node " + std::to_string(id));
        }

        if (node.kind() == RenderGraphNodeKind::Source) {
            if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
                const auto layer_id = source->layer_id();
                const RenderNode* expected = nullptr;
                if (layer_id.empty()) {
                    for (const auto& root : scene.nodes()) {
                        if (root.name == source->name()) { expected = &root; break; }
                    }
                } else if (const auto* layer = find_layer(resolved_by_name, layer_id)) {
                    if (layer->kind != LayerKind::Normal && layer->kind != LayerKind::Shape && layer->kind != LayerKind::Text) {
                        return topology_mismatch("source node layer kind changed for '" + std::string(layer_id) + "'");
                    }
                    if (layer->nodes.size() != 1) {
                        return topology_mismatch("single source node layer cardinality changed for '" + std::string(layer_id) + "'");
                    }
                    expected = &layer->nodes.front();
                }
                if (!expected) return missing_dynamic_data("source data missing for node '" + std::string(source->name()) + "'");
                if (info.shape_type >= 0 && info.shape_type != static_cast<int>(expected->shape.type())) {
                    return topology_mismatch("source shape type changed for node '" + std::string(source->name()) + "'");
                }
            } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
                const auto* layer = find_layer(resolved_by_name, multi->layer_id());
                if (!layer || layer->nodes.size() <= 1 ||
                    (layer->kind != LayerKind::Normal && layer->kind != LayerKind::Shape && layer->kind != LayerKind::Text)) {
                    return topology_mismatch("multi-source layer structure changed for '" + std::string(multi->layer_id()) + "'");
                }
                if (info.source_shape_types.size() != layer->nodes.size()) {
                    return topology_mismatch("multi-source item count changed for '" + std::string(multi->layer_id()) + "'");
                }
                for (size_t item = 0; item < layer->nodes.size(); ++item) {
                    if (info.source_shape_types[item] != static_cast<int>(layer->nodes[item].shape.type())) {
                        return topology_mismatch("multi-source shape type changed for '" + std::string(multi->layer_id()) + "'");
                    }
                }
            }
        } else if (node.kind() == RenderGraphNodeKind::Transform || node.kind() == RenderGraphNodeKind::Effect ||
                   node.kind() == RenderGraphNodeKind::TextRun) {
            if (!find_layer(resolved_by_name, node.layer_id())) {
                return missing_dynamic_data("refresh layer missing for node '" + std::string(node.name()) + "'");
            }
            if (node.kind() == RenderGraphNodeKind::TextRun) {
                const auto* layer = find_layer(resolved_by_name, node.layer_id());
                if (layer->kind != LayerKind::Text || layer->nodes.size() != 1 ||
                    layer->nodes.front().shape.type() != ShapeType::TextRun) {
                    return topology_mismatch("text-run structure changed for layer '" + std::string(node.layer_id()) + "'");
                }
            }
        }
    }
    return SceneRefreshResult{};
}

} // namespace

SceneRefreshResult refresh_compiled_graph_payloads(
    CompiledFrameGraph& compiled,
    const Scene& scene,
    RenderGraphContext& ctx,
    const LayerResolutionResult& resolved)
{
    // The graph is detached from CompiledGraphCache before this function is
    // called. Validate every structural invariant first; no mutating refresher
    // runs before this succeeds. A failed candidate is therefore discarded by
    // the coordinator instead of being published as a partial cache entry.
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

    const auto validation = validate_compiled_structure(compiled, scene, resolved_by_name);
    if (!validation) return validation;

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
                        || rl->layer->name != text.layer_id()) {
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
    return SceneRefreshResult{};
}

} // namespace chronon3d::graph::detail
