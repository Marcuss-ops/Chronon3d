// ═══════════════════════════════════════════════════════════════════════════
// frame_graph_builder.cpp — extracted private builders (FASE 16)
// ═══════════════════════════════════════════════════════════════════════════
//
// Member function definitions extracted from frame_graph_compiler.cpp.
// Kept here to make frame_graph_compiler.cpp focused on the public
// compile() / compile_with_reuse() / compute_structure_hash() surface.
//
// Builds:
//   - build_execution_levels()      — topological sort, levels, consumer_counts
//   - build_node_metadata()         — per-node fields, Merkle stable_node_id
//   - compute_resource_lifetimes()  — first_level/last_level per resource
//   - validate()                    — output node validity check

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace chronon3d::graph {

void FrameGraphCompiler::build_execution_levels(
    RenderGraph& graph,
    GraphNodeId output,
    CompiledFrameGraph& compiled
) const {
    const size_t node_count = graph.size();
    std::vector<char> reachable(node_count, 0);
    std::vector<GraphNodeId> stack{output};
    while (!stack.empty()) {
        GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id]) {
            continue;
        }
        reachable[id] = 1;
        for (GraphNodeId parent : graph.inputs(id)) {
            stack.push_back(parent);
        }
    }

    std::vector<std::vector<GraphNodeId>> children(node_count);
    std::vector<size_t> indegree(node_count, 0);
    compiled.consumer_counts.assign(node_count, 0);

    for (GraphNodeId child = 0; child < node_count; ++child) {
        if (!reachable[child]) {
            continue;
        }
        for (GraphNodeId parent : graph.inputs(child)) {
            if (!reachable[parent]) {
                continue;
            }
            children[parent].push_back(child);
            ++indegree[child];
            ++compiled.consumer_counts[parent];
        }
    }

    std::vector<GraphNodeId> current_level;
    current_level.reserve(node_count);
    for (GraphNodeId id = 0; id < node_count; ++id) {
        if (reachable[id] && indegree[id] == 0) {
            current_level.push_back(id);
        }
    }

    size_t scheduled = 0;
    while (!current_level.empty()) {
        compiled.levels.push_back(current_level);
        scheduled += current_level.size();

        std::vector<GraphNodeId> next_level;
        for (GraphNodeId id : current_level) {
            for (GraphNodeId child : children[id]) {
                if (--indegree[child] == 0) {
                    next_level.push_back(child);
                }
            }
        }
        current_level.swap(next_level);
    }

    const size_t reachable_count = static_cast<size_t>(
        std::count(reachable.begin(), reachable.end(), static_cast<char>(1))
    );
    if (scheduled != reachable_count) {
        throw std::runtime_error("FrameGraphCompiler: graph is not a DAG or contains unreachable dependency cycles");
    }
}

void FrameGraphCompiler::build_node_metadata(
    RenderGraph& graph,
    RenderGraphContext& ctx,
    CompiledFrameGraph& compiled,
    const FrameGraphCompileOptions& options
) const {
    const size_t node_count = graph.size();
    compiled.nodes.resize(node_count);

    std::vector<char> reachable(node_count, 0);
    for (const auto& level : compiled.levels) {
        for (GraphNodeId id : level) {
            reachable[id] = 1;
        }
    }

    std::vector<std::vector<GraphNodeId>> children(node_count);
    for (GraphNodeId child = 0; child < node_count; ++child) {
        if (!reachable[child]) continue;
        for (GraphNodeId parent : graph.inputs(child)) {
            if (!reachable[parent]) continue;
            children[parent].push_back(child);
        }
    }

    for (GraphNodeId id = 0; id < node_count; ++id) {
        auto& node_info = compiled.nodes[id];
        node_info.id = id;
        if (graph.has_node(id)) {
            auto& node = graph.node(id);
            node_info.name = node.name();
            node_info.layer_id = node.layer_id();
            node_info.kind = node.kind();
            node_info.inputs = graph.inputs(id);
            if (reachable[id]) {
                node_info.reachable = true;
                node_info.consumers = children[id];

                // Carry the builder's canonical layer location into the
                // compiled program.  The binding table used to depend on
                // metadata that no production builder ever populated, so
                // every real precomp program ended up with zero bindings and
                // its per-frame refresh path was silently skipped.
                const bool refreshable_kind =
                    node_info.kind == RenderGraphNodeKind::Source ||
                    node_info.kind == RenderGraphNodeKind::TextRun ||
                    node_info.kind == RenderGraphNodeKind::Transform ||
                    node_info.kind == RenderGraphNodeKind::Effect;
                const bool has_layer_location = node.layer_index() != UINT32_MAX;
                const bool is_root_source =
                    !has_layer_location && node.layer_id().empty() &&
                    (node_info.kind == RenderGraphNodeKind::Source ||
                     node_info.kind == RenderGraphNodeKind::TextRun);
                if (refreshable_kind && (has_layer_location || is_root_source)) {
                    node_info.binding_meta = SceneBindingMetadata{
                        true,
                        node.layer_index(),
                        node.item_index(),
                        0,
                        0,
                    };
                }

                node_info.cache_policy = node.cache_policy();

                // Capture structural payload discriminators once at compile
                // time. Refresh may replace dynamic content, matrices and
                // cache keys, but it must never change a node's render kind
                // or shape topology in place.
                if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
                    node_info.shape_type = static_cast<int>(source->render_node().shape.type());
                } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
                    node_info.shape_type = -2;
                    node_info.source_shape_types.reserve(multi->items().size());
                    for (const auto& item : multi->items()) {
                        if (!item.node) {
                            throw std::runtime_error(
                                "FrameGraphCompiler: multi-source node '" +
                                std::string(node.name()) +
                                "' contains a null renderable item");
                        }
                        node_info.source_shape_types.push_back(
                            static_cast<int>(item.node->shape.type()));
                    }
                } else if (const auto* text = dynamic_cast<const TextRunNode*>(&node)) {
                    node_info.shape_type = static_cast<int>(text->render_node().shape.type());
                }

                if (id < ctx.node_exec.early_exit_skip.size() && ctx.node_exec.early_exit_skip[id]) {
                    node_info.early_exit_skip = true;
                }
                if (options.compute_bboxes) {
                    node_info.predicted_bbox = node.predicted_bbox(ctx);
                }

                // ── Work Package 4 — derive stable_node_id ──
                const std::uint64_t layer_id_hash =
                    hash_string(node_info.layer_id);
                const std::uint64_t kind_and_name_hash = hash_combine(
                    static_cast<std::uint64_t>(node_info.kind),
                    hash_string(node_info.name)
                );
                node_info.stable_node_id = hash_stable_node_inputs(
                    layer_id_hash, kind_and_name_hash);
            }
        }
    }

    // ── Work Package 4.4 — input-aware refinement (Merkle-style) ────────
    // Resolve from sinks toward sources so consumer identities already carry
    // their own downstream context when they are folded into a producer.
    for (auto level_it = compiled.levels.rbegin();
         level_it != compiled.levels.rend(); ++level_it) {
        for (GraphNodeId id : *level_it) {
            if (id >= node_count) continue;
            auto& node_info = compiled.nodes[id];
            if (!node_info.reachable) continue;
            if (node_info.stable_node_id == kInvalidStableNodeId) continue;

            uint64_t h = node_info.stable_node_id.value;
            bool folded = false;

            std::vector<uint64_t> input_sids;
            input_sids.reserve(node_info.inputs.size());
            for (GraphNodeId input_id : node_info.inputs) {
                if (input_id >= node_count) continue;
                const auto& input_info = compiled.nodes[input_id];
                if (!input_info.reachable) continue;
                if (input_info.stable_node_id == kInvalidStableNodeId) continue;
                input_sids.push_back(input_info.stable_node_id.value);
            }
            std::sort(input_sids.begin(), input_sids.end());
            // Include both sides of the local graph neighbourhood.  A source
            // node can be materialised more than once for a derived pass
            // (for example, a shadow caster): those instances may have the
            // same layer/name/kind and no inputs, but they feed different
            // consumers.  Folding sorted consumer identities keeps the ID
            // content-derived without using the volatile graph-node index.
            std::vector<uint64_t> consumer_sids;
            consumer_sids.reserve(children[id].size());
            for (GraphNodeId child_id : children[id]) {
                if (child_id >= node_count) continue;
                const auto& child_info = compiled.nodes[child_id];
                if (!child_info.reachable ||
                    child_info.stable_node_id == kInvalidStableNodeId) {
                    continue;
                }
                consumer_sids.push_back(child_info.stable_node_id.value);
            }
            std::sort(consumer_sids.begin(), consumer_sids.end());

            constexpr uint64_t kInputDomain = 0x494e505554ULL;    // INPUT
            constexpr uint64_t kConsumerDomain = 0x434f4e53554d4552ULL; // CONSUMER
            h ^= kInputDomain;
            h *= 0x100000001b3ULL;
            h ^= static_cast<uint64_t>(input_sids.size());
            h *= 0x100000001b3ULL;
            for (uint64_t sid : input_sids) {
                h ^= sid;
                h *= 0x100000001b3ULL;
                folded = true;
            }
            h ^= kConsumerDomain;
            h *= 0x100000001b3ULL;
            h ^= static_cast<uint64_t>(consumer_sids.size());
            h *= 0x100000001b3ULL;
            for (uint64_t sid : consumer_sids) {
                h ^= sid;
                h *= 0x100000001b3ULL;
                folded = true;
            }
            if (folded) {
                node_info.stable_node_id = StableNodeId{h == 0u ? 1u : h};
            }
        }
    }

    // ── Work Package 4 — collision detection ─────────────────────────────
    {
        std::unordered_map<StableNodeId, GraphNodeId> seen;
        for (size_t i = 0; i < compiled.nodes.size(); ++i) {
            if (!compiled.nodes[i].reachable) continue;
            const auto sid = compiled.nodes[i].stable_node_id;
            if (sid == kInvalidStableNodeId) continue;
            auto [it, inserted] = seen.emplace(sid, static_cast<GraphNodeId>(i));
            if (!inserted) {
                const auto describe_consumers = [&](GraphNodeId node_id) {
                    std::string text;
                    for (GraphNodeId child_id : compiled.nodes[node_id].consumers) {
                        if (!text.empty()) text += ",";
                        text += std::to_string(child_id);
                        if (child_id < compiled.nodes.size()) {
                            text += ":" + compiled.nodes[child_id].name;
                        }
                    }
                    return text.empty() ? std::string{"-"} : text;
                };
                throw std::runtime_error(
                    "FrameGraphCompiler: stable_node_id collision between nodes "
                    + std::to_string(it->second) + " (layer='"
                    + compiled.nodes[it->second].layer_id + "', name='"
                    + compiled.nodes[it->second].name + "', kind="
                    + std::to_string(static_cast<int>(compiled.nodes[it->second].kind))
                    + ") and " + std::to_string(i) + " (layer='"
                    + compiled.nodes[i].layer_id + "', name='"
                    + compiled.nodes[i].name + "', kind="
                    + std::to_string(static_cast<int>(compiled.nodes[i].kind))
                    + ", consumers=" + describe_consumers(i) + ")"
                    + "; first_consumers=" + describe_consumers(it->second));
            }
        }
    }
}

void FrameGraphCompiler::validate_renderable_shape(
    const ::chronon3d::RenderNode& render_node,
    const CompiledNodeInfo& node_info,
    const RenderGraphContext& ctx
) const {
    const auto shape_type = render_node.shape.type();
    if (shape_type == ShapeType::None) {
        throw std::runtime_error(
            "FrameGraphCompiler: renderable Shape node '" +
            node_info.name + "' has ShapeType::None");
    }

    if (!ctx.services.backend) {
        throw std::runtime_error(
            "FrameGraphCompiler: renderable Shape node '" +
            node_info.name + "' has no render backend");
    }
    if (const auto error = ctx.services.backend->validate_render_node(render_node)) {
        throw std::runtime_error(
            "FrameGraphCompiler: renderable Shape node '" +
            node_info.name + "' is invalid: " + error->message);
    }
}

void FrameGraphCompiler::validate_renderable_graph(
    const RenderGraph& graph,
    GraphNodeId output,
    const RenderGraphContext& ctx
) const {
    const size_t node_count = graph.size();
    std::vector<char> reachable(node_count, 0);
    std::vector<GraphNodeId> stack{output};
    while (!stack.empty()) {
        const GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id]) {
            continue;
        }
        reachable[id] = 1;
        for (const GraphNodeId parent : graph.inputs(id)) {
            stack.push_back(parent);
        }
    }

    for (GraphNodeId id = 0; id < node_count; ++id) {
        if (!reachable[id] || !graph.has_node(id)) {
            continue;
        }
        const auto& node = graph.node(id);
        if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
            CompiledNodeInfo info;
            info.id = id;
            info.name = node.name();
            validate_renderable_shape(source->render_node(), info, ctx);
        } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
            CompiledNodeInfo info;
            info.id = id;
            info.name = node.name();
            for (const auto& item : multi->items()) {
                if (!item.node) {
                    throw std::runtime_error(
                        "FrameGraphCompiler: multi-source node '" +
                        std::string(node.name()) +
                        "' contains a null renderable item");
                }
                validate_renderable_shape(*item.node, info, ctx);
            }
        }
    }
}

void FrameGraphCompiler::compute_resource_lifetimes(
    CompiledFrameGraph& compiled
) const {
    const size_t node_count = compiled.graph.size();
    compiled.lifetimes.assign(node_count, ResourceLifetime{});

    for (size_t level_index = 0; level_index < compiled.levels.size(); ++level_index) {
        for (GraphNodeId node_id : compiled.levels[level_index]) {
            if (node_id < node_count) {
                compiled.lifetimes[node_id].producer = node_id;
                compiled.lifetimes[node_id].first_level = level_index;
                compiled.lifetimes[node_id].last_level = level_index;
            }
        }
    }

    for (size_t level_index = 0; level_index < compiled.levels.size(); ++level_index) {
        for (GraphNodeId node_id : compiled.levels[level_index]) {
            if (node_id >= node_count) continue;
            for (GraphNodeId input_id : compiled.nodes[node_id].inputs) {
                if (input_id < node_count) {
                    compiled.lifetimes[input_id].last_level = std::max(
                        compiled.lifetimes[input_id].last_level, level_index
                    );
                    compiled.lifetimes[input_id].consumer_count++;
                }
            }
        }
    }
}

void FrameGraphCompiler::validate(
    const CompiledFrameGraph& compiled
) const {
    if (compiled.output == k_invalid_node) {
        throw std::runtime_error("FrameGraphCompiler: invalid output node");
    }
}

} // namespace chronon3d::graph
