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

                node_info.cache_policy = node.cache_policy();

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
    for (const auto& level : compiled.levels) {
        for (GraphNodeId id : level) {
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
            for (uint64_t sid : input_sids) {
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
                throw std::runtime_error(
                    "FrameGraphCompiler: stable_node_id collision between nodes "
                    + std::to_string(it->second) + " and " + std::to_string(i));
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
