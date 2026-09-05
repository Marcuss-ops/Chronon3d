#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>

#include "refresh/source.hpp"
#include "refresh/multi_source.hpp"
#include "refresh/effect_stack.hpp"
#include "refresh/transform.hpp"
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include "../builder/graph_builder_coordinates.hpp"
#include "refresh/layer_item.hpp"
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

#include "../builder/graph_builder_pipeline.hpp"
#include <chronon3d/scene/model/core/scene.hpp>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace chronon3d::graph::detail {

namespace {

SceneRefreshResult topology_mismatch(std::string message) {
    return SceneRefreshResult{SceneRefreshStatus::TopologyMismatch, std::move(message)};
}

SceneRefreshResult missing_processor(std::string message) {
    return SceneRefreshResult{SceneRefreshStatus::MissingProcessor, std::move(message)};
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
    if (compiled.graph.phase() != GraphPhase::Building) {
        return topology_mismatch("compiled graph is immutable; refresh requires a mutable graph candidate");
    }
    if (compiled.nodes.size() < compiled.graph.size()) {
        return topology_mismatch("compiled node metadata is shorter than the graph");
    }

    for (size_t id = 0; id < compiled.graph.size(); ++id) {
        if (!compiled.graph.has_node(static_cast<GraphNodeId>(id))) continue;
        const auto& node = compiled.graph.node(static_cast<GraphNodeId>(id));
        const auto& info = compiled.nodes[id];
        if (!info.reachable) continue;
        const auto graph_inputs = compiled.graph.inputs(static_cast<GraphNodeId>(id));
        if (info.id != static_cast<GraphNodeId>(id) ||
            info.kind != node.kind() ||
            info.name != node.name() ||
            info.layer_id != node.layer_id() ||
            info.inputs != graph_inputs ||
            info.cache_policy.mode != node.cache_policy().mode ||
            info.cache_policy.invalidation != node.cache_policy().invalidation ||
            info.cache_policy.reason != node.cache_policy().reason) {
            return topology_mismatch("compiled graph metadata does not match node topology at node " + std::to_string(id));
        }

        std::vector<GraphNodeId> expected_consumers;
        expected_consumers.reserve(compiled.graph.size());
        for (GraphNodeId consumer = 0; consumer < compiled.graph.size(); ++consumer) {
            if (!compiled.graph.has_node(consumer) ||
                consumer >= compiled.nodes.size() || !compiled.nodes[consumer].reachable) {
                continue;
            }
            const auto& inputs = compiled.graph.inputs(consumer);
            if (std::find(inputs.begin(), inputs.end(), static_cast<GraphNodeId>(id)) != inputs.end()) {
                expected_consumers.push_back(consumer);
            }
        }
        if (info.consumers != expected_consumers) {
            return topology_mismatch("compiled graph consumer metadata does not match node topology at node " + std::to_string(id));
        }

        if (const auto* source = dynamic_cast<const SourceNode*>(&node);
            source && source->render_node().shape.type() == ShapeType::None) {
            return SceneRefreshResult{SceneRefreshStatus::InvalidRenderableNode,
                "cached source renderable node has ShapeType::None at node " +
                std::to_string(id)};
        }

        const auto expected_processor_id = [&]() {
            if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
                return std::string{"source:"} + std::to_string(
                    static_cast<int>(source->render_node().shape.type()));
            }
            if (dynamic_cast<const MultiSourceNode*>(&node)) return std::string{"multi_source"};
            if (dynamic_cast<const TextRunNode*>(&node)) return std::string{"text_run"};
            return std::string{to_string(node.kind())};
        }();
        if (info.processor_id.empty()) {
            return missing_processor("compiled processor identity is missing at node " + std::to_string(id));
        }
        if (info.processor_id != expected_processor_id) {
            return topology_mismatch("compiled processor identity does not match node topology at node " + std::to_string(id));
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
                if (expected->shape.type() == ShapeType::None) {
                    return SceneRefreshResult{SceneRefreshStatus::InvalidRenderableNode,
                        "source renderable node has ShapeType::None for '" +
                        std::string(source->name()) + "'"};
                }
                if (info.shape_type != static_cast<int>(source->render_node().shape.type()) ||
                    info.shape_type != static_cast<int>(expected->shape.type())) {
                    return topology_mismatch("source shape type changed for node '" + std::string(source->name()) + "'");
                }
            } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
                const auto* layer = find_layer(resolved_by_name, multi->layer_id());
                if (!layer || layer->nodes.size() <= 1 ||
                    (layer->kind != LayerKind::Normal && layer->kind != LayerKind::Shape && layer->kind != LayerKind::Text)) {
                    return topology_mismatch("multi-source layer structure changed for '" + std::string(multi->layer_id()) + "'");
                }
                if (info.shape_type != -2 ||
                    info.source_shape_types.size() != multi->items().size() ||
                    info.source_shape_types.size() != layer->nodes.size()) {
                    return topology_mismatch("multi-source item count changed for '" + std::string(multi->layer_id()) + "'");
                }
                for (size_t item = 0; item < multi->items().size(); ++item) {
                    if (!multi->items()[item].node ||
                        info.source_shape_types[item] != static_cast<int>(multi->items()[item].node->shape.type())) {
                        return topology_mismatch("cached multi-source shape topology changed for '" +
                            std::string(multi->layer_id()) + "'");
                    }
                    if (item >= layer->nodes.size() ||
                        multi->items()[item].node->name != layer->nodes[item].name) {
                        return topology_mismatch("cached multi-source item identity changed for '" +
                            std::string(multi->layer_id()) + "'");
                    }
                }
                for (size_t item = 0; item < layer->nodes.size(); ++item) {
                    if (layer->nodes[item].shape.type() == ShapeType::None) {
                        return SceneRefreshResult{SceneRefreshStatus::InvalidRenderableNode,
                            "multi-source renderable node has ShapeType::None for '" +
                            std::string(multi->layer_id()) + "'"};
                    }
                    if (info.source_shape_types[item] != static_cast<int>(layer->nodes[item].shape.type())) {
                        return topology_mismatch("multi-source shape type changed for '" + std::string(multi->layer_id()) + "'");
                    }
                }
            } else {
                return missing_processor("source graph node has no registered processor at node " +
                    std::to_string(id));
            }
        } else if (node.kind() == RenderGraphNodeKind::Transform || node.kind() == RenderGraphNodeKind::Effect ||
                   node.kind() == RenderGraphNodeKind::TextRun) {
            if (node.kind() == RenderGraphNodeKind::Transform &&
                dynamic_cast<const TransformNode*>(&node) == nullptr) {
                return missing_processor("transform graph node has no registered processor at node " +
                    std::to_string(id));
            }
            if (node.kind() == RenderGraphNodeKind::Effect &&
                dynamic_cast<const EffectStackNode*>(&node) == nullptr) {
                return missing_processor("effect graph node has no registered processor at node " +
                    std::to_string(id));
            }
            if (node.kind() == RenderGraphNodeKind::TextRun &&
                dynamic_cast<const TextRunNode*>(&node) == nullptr) {
                return missing_processor("text-run graph node has no registered processor at node " +
                    std::to_string(id));
            }
            if (!find_layer(resolved_by_name, node.layer_id())) {
                return missing_dynamic_data("refresh layer missing for node '" + std::string(node.name()) + "'");
            }
            if (node.kind() == RenderGraphNodeKind::TextRun) {
                const auto* layer = find_layer(resolved_by_name, node.layer_id());
                if (layer->kind != LayerKind::Text || layer->nodes.size() != 1 ||
                    layer->nodes.front().shape.type() != ShapeType::TextRun) {
                    return topology_mismatch("text-run structure changed for layer '" + std::string(node.layer_id()) + "'");
                }
                const auto* text_node = dynamic_cast<const TextRunNode*>(&node);
                if (info.shape_type < 0 ||
                    !text_node ||
                    info.shape_type != static_cast<int>(text_node->render_node().shape.type()) ||
                    info.shape_type != static_cast<int>(layer->nodes.front().shape.type())) {
                    return topology_mismatch("text-run shape type changed for layer '" +
                        std::string(node.layer_id()) + "'");
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

    // ── 4. Build the complete refresh patch ─────────────────────────────
    // Prepare each dynamic payload on a copy of its original node. Helpers
    // can allocate and mutate these copies freely; the compiled graph remains
    // untouched until every node has been prepared successfully.
    struct NodeRefreshPatch {
        GraphNodeId id{k_invalid_node};
        std::variant<
            std::unique_ptr<SourceNode>,
            std::unique_ptr<MultiSourceNode>,
            std::unique_ptr<EffectStackNode>,
            std::unique_ptr<TransformNode>,
            std::unique_ptr<TextRunNode>> prepared;
    };
    struct GraphRefreshPatch {
        std::vector<NodeRefreshPatch> nodes;
    } patch;
    patch.nodes.reserve(compiled.graph.live_count());

    for (size_t id = 0; id < compiled.graph.size(); ++id) {
        const auto node_id = static_cast<GraphNodeId>(id);
        if (!compiled.graph.has_node(node_id) ||
            node_id >= compiled.nodes.size() || !compiled.nodes[node_id].reachable) {
            continue;
        }
        const auto& graph_node = compiled.graph.node(node_id);

        switch (graph_node.kind()) {
            case RenderGraphNodeKind::Source:
                if (const auto* source_node = dynamic_cast<const SourceNode*>(&graph_node)) {
                    auto prepared = std::make_unique<SourceNode>(*source_node);
                    refresh_source_node(*prepared, resolved_by_name,
                        root_nodes_by_name, is_static_cache, ctx);
                    patch.nodes.push_back({node_id, std::move(prepared)});
                } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&graph_node)) {
                    auto prepared = std::make_unique<MultiSourceNode>(*multi);
                    refresh_multi_source_node(*prepared, resolved_by_name,
                        is_static_cache, ctx);
                    patch.nodes.push_back({node_id, std::move(prepared)});
                } else {
                    return topology_mismatch("source graph node has an unknown concrete implementation at node " + std::to_string(id));
                }
                break;
            case RenderGraphNodeKind::Effect: {
                const auto* effect_node = dynamic_cast<const EffectStackNode*>(&graph_node);
                if (!effect_node) {
                    return topology_mismatch("effect graph node has an invalid concrete implementation at node " + std::to_string(id));
                }
                auto prepared = std::make_unique<EffectStackNode>(*effect_node);
                refresh_effect_stack_node(*prepared, resolved_by_name, ctx);
                patch.nodes.push_back({node_id, std::move(prepared)});
                break;
            }
            case RenderGraphNodeKind::Transform: {
                const auto* transform_node = dynamic_cast<const TransformNode*>(&graph_node);
                if (!transform_node) {
                    return topology_mismatch("transform graph node has an invalid concrete implementation at node " + std::to_string(id));
                }
                auto prepared = std::make_unique<TransformNode>(*transform_node);
                refresh_transform_node(*prepared, resolved_by_name, ctx);
                patch.nodes.push_back({node_id, std::move(prepared)});
                break;
            }
            case RenderGraphNodeKind::TextRun: {
                const auto* text_node = dynamic_cast<const TextRunNode*>(&graph_node);
                if (!text_node) {
                    return topology_mismatch("text-run graph node has an invalid concrete implementation at node " + std::to_string(id));
                }
                auto prepared = std::make_unique<TextRunNode>(*text_node);
                bool found = false;
                for (const auto& [name, rl] : resolved_by_name) {
                    if (!rl || !rl->layer || rl->layer->kind != LayerKind::Text
                        || rl->layer->nodes.size() != 1
                        || rl->layer->name != prepared->layer_id()) {
                        continue;
                    }
                    const auto& render_ref = rl->layer->nodes[0];
                    const auto item = make_layer_graph_item_for_refresh(*rl, ctx);
                    f32 opacity = 1.0f;
                    const auto placement = resolve_text_run_placement(
                        item, render_ref, ctx, opacity);
                    const std::string layer_name_str(rl->layer->name);
                    const bool item_static = is_static_cache.count(layer_name_str)
                        ? is_static_cache.at(layer_name_str) : rl->layer->cache_static;
                    const bool source_is_static = item_static;
                    // Canonical cache identity via the builder. Camera is
                    // intentionally NOT folded here (parity with the fresh
                    // build path: projected text matrices are owned by the
                    // downstream TransformNode, not the text raster cache).
                    cache::NodeCacheKey key = cache::NodeCacheIdentityBuilder{
                        "layer.textrun:" + name + ":" +
                        std::string(render_ref.name)
                    }
                        .frame(source_is_static ? Frame{0} : ctx.frame_input.frame)
                        .output(ctx.frame_input.width, ctx.frame_input.height)
                        .params(hash_render_node_content_only(render_ref))
                        .source(hash_combine(
                            hash_string(render_ref.name),
                            hash_render_node_placement_only(render_ref)))
                        .build();
                    prepared->refresh_placement(
                        render_ref, placement, key,
                        std::optional<f32>(opacity));
                    found = true;
                    break;
                }
                if (!found) {
                    return missing_dynamic_data("text-run refresh data missing for node '" +
                        std::string(graph_node.name()) + "'");
                }
                patch.nodes.push_back({node_id, std::move(prepared)});
                break;
            }
            case RenderGraphNodeKind::Mask:
            case RenderGraphNodeKind::Composite:
            case RenderGraphNodeKind::Precomp:
            case RenderGraphNodeKind::Video:
            case RenderGraphNodeKind::Adjustment:
            case RenderGraphNodeKind::MotionBlur:
            case RenderGraphNodeKind::ColorConvert:
            case RenderGraphNodeKind::TrackMatte:
            case RenderGraphNodeKind::Output:
            case RenderGraphNodeKind::Transition:
            case RenderGraphNodeKind::ClipTransition:
                // These node kinds have no dynamic scene refresher in this
                // path; their compiled processor identity was validated above.
                break;
            default:
                return missing_processor("graph node kind has no registered scene refresher at node " +
                    std::to_string(id));
        }
    }

    // ── 5. Validate the prepared patch before commit ────────────────────
    // Refresh helpers may update dynamic payloads, but they must preserve
    // every structural discriminator and the node cache policy captured by
    // the compiler. If a helper ever attempts to cross that boundary, fail
    // without touching the original graph.
    for (const auto& node_patch : patch.nodes) {
        if (node_patch.id >= compiled.nodes.size()) {
            return topology_mismatch("refresh patch contains an unknown node id " +
                std::to_string(node_patch.id));
        }
        const auto& original_info = compiled.nodes[node_patch.id];
        bool structural_mismatch = false;
        bool invalid_renderable = false;
        std::visit([&](const auto& prepared) {
            using Prepared = std::decay_t<decltype(prepared)>;
            if (!prepared) {
                structural_mismatch = true;
                return;
            }
            const auto& prepared_node = *prepared;
            std::string prepared_processor_id;
            if (const auto* source = dynamic_cast<const SourceNode*>(&prepared_node)) {
                const auto shape_type = source->render_node().shape.type();
                invalid_renderable = shape_type == ShapeType::None;
                prepared_processor_id = "source:" +
                    std::to_string(static_cast<int>(shape_type));
                structural_mismatch = original_info.shape_type != static_cast<int>(shape_type);
            } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&prepared_node)) {
                prepared_processor_id = "multi_source";
                structural_mismatch = original_info.shape_type != -2 ||
                    original_info.source_shape_types.size() != multi->items().size();
                for (size_t index = 0; !structural_mismatch && index < multi->items().size(); ++index) {
                    if (!multi->items()[index].node) {
                        invalid_renderable = true;
                        break;
                    }
                    if (index >= original_info.source_shape_types.size() ||
                        original_info.source_shape_types[index] !=
                        static_cast<int>(multi->items()[index].node->shape.type())) {
                        structural_mismatch = true;
                    }
                }
            } else if (const auto* text = dynamic_cast<const TextRunNode*>(&prepared_node)) {
                prepared_processor_id = "text_run";
                structural_mismatch = original_info.shape_type !=
                    static_cast<int>(text->render_node().shape.type());
            } else {
                prepared_processor_id = std::string(to_string(prepared_node.kind()));
            }
            const auto& binding = original_info.binding_meta;
            const bool binding_mismatch = binding.active &&
                (prepared_node.layer_index() != binding.layer_index ||
                 prepared_node.item_index() != binding.item_index);
            structural_mismatch = structural_mismatch ||
                prepared_processor_id != original_info.processor_id ||
                prepared_node.kind() != original_info.kind ||
                prepared_node.name() != original_info.name ||
                prepared_node.layer_id() != original_info.layer_id ||
                binding_mismatch ||
                prepared_node.cache_policy().mode != original_info.cache_policy.mode ||
                prepared_node.cache_policy().invalidation != original_info.cache_policy.invalidation ||
                prepared_node.cache_policy().reason != original_info.cache_policy.reason;
        }, node_patch.prepared);
        if (invalid_renderable) {
            return SceneRefreshResult{SceneRefreshStatus::InvalidRenderableNode,
                "refresh prepared an invalid renderable node at node " +
                std::to_string(node_patch.id)};
        }
        if (structural_mismatch) {
            return topology_mismatch(
                "scene refresh prepared a node with changed structural metadata at node " +
                std::to_string(node_patch.id));
        }
    }

    // ── 6. Commit the complete patch ────────────────────────────────────
    // No refresh helper runs here. Each assignment transfers an already
    // prepared typed state into its original node; all fallible preparation
    // has completed before the first original node is touched.
    for (auto& node_patch : patch.nodes) {
        std::visit([&](auto& prepared) {
            using Prepared = std::decay_t<decltype(prepared)>;
            if constexpr (std::is_same_v<Prepared, std::unique_ptr<SourceNode>> ||
                          std::is_same_v<Prepared, std::unique_ptr<MultiSourceNode>> ||
                          std::is_same_v<Prepared, std::unique_ptr<EffectStackNode>> ||
                          std::is_same_v<Prepared, std::unique_ptr<TransformNode>> ||
                          std::is_same_v<Prepared, std::unique_ptr<TextRunNode>>) {
                compiled.graph.replace_node(node_patch.id, std::move(prepared));
            }
        }, node_patch.prepared);
    }
    return SceneRefreshResult{};
}

} // namespace chronon3d::graph::detail
