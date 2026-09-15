#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>

#include "refresh/source.hpp"
#include "refresh/multi_source.hpp"
#include "refresh/effect_stack.hpp"
#include "refresh/transform.hpp"
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include "../builder/graph_builder_coordinates.hpp"
#include "refresh/layer_item.hpp"
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/core/profiling/counters.hpp>

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

template <typename Node>
Node* acquire_refresh_node(
    std::vector<std::unique_ptr<RenderGraphNode>>& scratch,
    GraphNodeId id,
    const Node& original,
    RenderCounters* counters)
{
    auto& spare = scratch[id];
    if (auto* typed = dynamic_cast<Node*>(spare.get())) {
        // Keep the spare allocation and its vector/string capacities.  The
        // copy is still isolated from the live graph until the commit swap.
        *typed = original;
        if (counters) {
            counters->graph_refresh_scratch_node_reuses.fetch_add(
                1, std::memory_order_relaxed);
        }
        return typed;
    }
    spare = std::make_unique<Node>(original);
    if (counters) {
        counters->graph_refresh_scratch_node_builds.fetch_add(
            1, std::memory_order_relaxed);
    }
    return static_cast<Node*>(spare.get());
}

SceneRefreshResult topology_mismatch(std::string message) {
    return SceneRefreshResult{SceneRefreshStatus::TopologyMismatch, std::move(message)};
}

SceneRefreshResult missing_processor(std::string message) {
    return SceneRefreshResult{SceneRefreshStatus::MissingProcessor, std::move(message)};
}

SceneRefreshResult missing_dynamic_data(std::string message) {
    return SceneRefreshResult{SceneRefreshStatus::MissingDynamicData, std::move(message)};
}

const Layer* find_layer(const std::unordered_map<std::string_view, const ResolvedLayer*>& resolved_by_name,
                       std::string_view name) {
    const auto it = resolved_by_name.find(name);
    return it == resolved_by_name.end() || !it->second ? nullptr : it->second->layer;
}

SceneRefreshResult validate_compiled_structure(
    const CompiledFrameGraph& compiled,
    const Scene& scene,
    const std::unordered_map<std::string_view, const ResolvedLayer*>& resolved_by_name)
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
    if (!compiled.refresh_static_cache_ready) {
        compiled.refresh_static_cache.clear();
        compute_static_layers(resolved, compiled.refresh_static_cache);
        compiled.refresh_static_cache_ready = true;
    }
    const auto& is_static_cache = compiled.refresh_static_cache;

    // ── 2. Build resolved layer lookup (by name) ─────────────────────────
    auto& resolved_by_name = compiled.refresh_resolved_by_name;
    resolved_by_name.clear();
    resolved_by_name.reserve(resolved.layers.size());
    for (const auto& rl : resolved.layers) {
        if (rl.layer) {
            resolved_by_name.emplace(rl.layer->name, &rl);
        }
    }

    // ── 3. Build scene root node lookup (by name) ────────────────────────
    auto& root_nodes_by_name = compiled.refresh_root_nodes_by_name;
    root_nodes_by_name.clear();
    root_nodes_by_name.reserve(scene.nodes().size());
    for (const auto& node : scene.nodes()) {
        root_nodes_by_name.emplace(node.name, &node);
    }

    const auto validation = validate_compiled_structure(compiled, scene, resolved_by_name);
    if (!validation) return validation;

    // ── 4. Prepare the complete refresh patch ───────────────────────────
    // `refresh_scratch` owns one detached spare per dynamic node.  On the
    // first refresh a spare is cloned; subsequent refreshes copy into the
    // same typed object and retain its capacities.  The live graph remains
    // untouched until the commit loop below.
    compiled.refresh_scratch.resize(compiled.graph.size());

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
                    auto* prepared = acquire_refresh_node(
                        compiled.refresh_scratch, node_id, *source_node,
                        ctx.node_exec.counters);
                    refresh_source_node(*prepared, resolved_by_name,
                        root_nodes_by_name, is_static_cache, ctx);
                } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&graph_node)) {
                    auto* prepared = acquire_refresh_node(
                        compiled.refresh_scratch, node_id, *multi,
                        ctx.node_exec.counters);
                    refresh_multi_source_node(*prepared, resolved_by_name,
                        is_static_cache, ctx);
                } else {
                    return topology_mismatch("source graph node has an unknown concrete implementation at node " + std::to_string(id));
                }
                break;
            case RenderGraphNodeKind::Effect: {
                const auto* effect_node = dynamic_cast<const EffectStackNode*>(&graph_node);
                if (!effect_node) {
                    return topology_mismatch("effect graph node has an invalid concrete implementation at node " + std::to_string(id));
                }
                auto* prepared = acquire_refresh_node(
                    compiled.refresh_scratch, node_id, *effect_node,
                    ctx.node_exec.counters);
                refresh_effect_stack_node(*prepared, resolved_by_name, ctx);
                break;
            }
            case RenderGraphNodeKind::Transform: {
                const auto* transform_node = dynamic_cast<const TransformNode*>(&graph_node);
                if (!transform_node) {
                    return topology_mismatch("transform graph node has an invalid concrete implementation at node " + std::to_string(id));
                }
                auto* prepared = acquire_refresh_node(
                    compiled.refresh_scratch, node_id, *transform_node,
                    ctx.node_exec.counters);
                bool typed_applied = false;
                if (ctx.frame_parameters &&
                    compiled.nodes[node_id].binding_meta.has_binding()) {
                    const auto layer_index = compiled.nodes[node_id].binding_meta.layer_index;
                    const auto& typed_layers = ctx.frame_parameters->layers;
                    if (layer_index < typed_layers.size()) {
                        const auto& parameters = typed_layers[layer_index];
                        if (parameters.active) {
                            prepared->set_matrix(parameters.matrix);
                            prepared->set_opacity(parameters.opacity);
                            typed_applied = true;
                        }
                    }
                }
                if (!typed_applied && !ctx.frame_parameters) {
                    refresh_transform_node(*prepared, resolved_by_name, ctx);
                }
                if (!typed_applied && ctx.frame_parameters) {
                    return missing_dynamic_data(
                        "typed transform parameters missing for node " +
                        std::to_string(id));
                }
                break;
            }
            case RenderGraphNodeKind::TextRun: {
                const auto* text_node = dynamic_cast<const TextRunNode*>(&graph_node);
                if (!text_node) {
                    return topology_mismatch("text-run graph node has an invalid concrete implementation at node " + std::to_string(id));
                }
                auto* prepared = acquire_refresh_node(
                    compiled.refresh_scratch, node_id, *text_node,
                    ctx.node_exec.counters);
                // Text nodes are keyed by their owning layer.  The previous
                // refresh path scanned every resolved layer for every text
                // node, turning a typewriter refresh into O(text_nodes ×
                // layers) work and needlessly rebuilding temporary strings.
                // The canonical name index already exists; use it directly.
                const std::string layer_id{prepared->layer_id()};
                const auto layer_it = resolved_by_name.find(layer_id);
                const auto* rl = layer_it == resolved_by_name.end()
                    ? nullptr : layer_it->second;
                if (rl && rl->layer && rl->layer->kind == LayerKind::Text &&
                    rl->layer->nodes.size() == 1) {
                    const auto& render_ref = rl->layer->nodes[0];
                    const auto item = make_layer_graph_item_for_refresh(*rl, ctx);
                    f32 opacity = 1.0f;
                    const auto placement = resolve_text_run_placement(
                        item, render_ref, ctx, opacity);
                    const std::string layer_name_str(rl->layer->name);
                    // AUDIT F12 — one hash per layer instead of two: the
                    // previous count()-then-at() pair hashed the layer name
                    // twice per refresh; a single find() hashes once and
                    // keeps the fallback bit-identical.
                    const auto static_it = is_static_cache.find(layer_name_str);
                    const bool item_static = static_it != is_static_cache.end()
                        ? static_it->second : rl->layer->cache_static;
                    const bool source_is_static = item_static;
                    // Canonical cache identity via the builder. Camera is
                    // intentionally NOT folded here (parity with the fresh
                    // build path: projected text matrices are owned by the
                    // downstream TransformNode, not the text raster cache).
                    cache::NodeCacheKey key = cache::NodeCacheIdentityBuilder{
                        "layer.textrun:" + layer_id + ":" +
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
                }
                if (!rl || !rl->layer || rl->layer->kind != LayerKind::Text ||
                    rl->layer->nodes.size() != 1) {
                    return missing_dynamic_data("text-run refresh data missing for node '" +
                        std::string(graph_node.name()) + "'");
                }
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
    for (size_t id = 0; id < compiled.graph.size(); ++id) {
        const auto node_id = static_cast<GraphNodeId>(id);
        if (!compiled.graph.has_node(node_id) ||
            node_id >= compiled.nodes.size() || !compiled.nodes[node_id].reachable ||
            !compiled.refresh_scratch[node_id]) {
            continue;
        }
        const auto& original_info = compiled.nodes[node_id];
        bool structural_mismatch = false;
        bool invalid_renderable = false;
        const auto& prepared_node = *compiled.refresh_scratch[node_id];
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
        if (invalid_renderable) {
            return SceneRefreshResult{SceneRefreshStatus::InvalidRenderableNode,
                "refresh prepared an invalid renderable node at node " +
                std::to_string(node_id)};
        }
        if (structural_mismatch) {
            return topology_mismatch(
                "scene refresh prepared a node with changed structural metadata at node " +
                std::to_string(node_id));
        }
    }

    // ── 6. Commit the complete patch ────────────────────────────────────
    // No refresh helper runs here.  Swap each prepared node into the graph;
    // the displaced node becomes the spare for the next frame.  Unique_ptr
    // moves are non-allocating, and every fallible operation has completed
    // before the first live node is touched.
    for (size_t id = 0; id < compiled.graph.size(); ++id) {
        const auto node_id = static_cast<GraphNodeId>(id);
        if (!compiled.graph.has_node(node_id) ||
            node_id >= compiled.nodes.size() || !compiled.nodes[node_id].reachable ||
            !compiled.refresh_scratch[node_id]) {
            continue;
        }
        compiled.refresh_scratch[node_id] =
            compiled.graph.replace_node(
                node_id, std::move(compiled.refresh_scratch[node_id]));
    }
    return SceneRefreshResult{};
}

} // namespace chronon3d::graph::detail
