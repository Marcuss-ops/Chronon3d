#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <algorithm>
#include <unordered_set>

namespace chronon3d::graph::optimizer {

// ── Helper: check if two transform nodes can be fused ───────────────────
static bool can_fuse_transforms(const RenderGraph& graph, GraphNodeId child_id) {
    if (!graph.has_node(child_id)) return false;
    const auto& inputs = graph.inputs(child_id);
    if (inputs.size() != 1) return false;

    GraphNodeId parent_id = inputs[0];
    if (!graph.has_node(parent_id)) return false;

    const auto& child  = graph.node(child_id);
    const auto& parent = graph.node(parent_id);

    if (parent.kind() != RenderGraphNodeKind::Transform) return false;
    if (child.kind()  != RenderGraphNodeKind::Transform) return false;
    if (parent.frame_dependent() != child.frame_dependent()) return false;
    return true;
}

// ── Helper: count consumers of a node ───────────────────────────────────
static size_t count_consumers(const RenderGraph& graph, GraphNodeId id) {
    size_t count = 0;
    for (GraphNodeId n = 0; n < static_cast<GraphNodeId>(graph.size()); ++n) {
        if (!graph.has_node(n)) continue;
        for (GraphNodeId in : graph.inputs(n)) {
            if (in == id) ++count;
        }
    }
    return count;
}

// ── Fusion main: actually rewires the graph ─────────────────────────────
size_t fuse_nodes(RenderGraph& graph) {
    const size_t node_count = graph.size();
    if (node_count < 2) return 0;

    std::vector<bool> absorbed(node_count, false);

    // Build consumer counts
    std::vector<size_t> consumers(node_count, 0);
    for (GraphNodeId n = 0; n < static_cast<GraphNodeId>(node_count); ++n) {
        if (!graph.has_node(n)) continue;
        for (GraphNodeId in : graph.inputs(n)) {
            if (in < node_count) consumers[in]++;
        }
    }

    for (GraphNodeId child_id = 0; child_id < static_cast<GraphNodeId>(node_count); ++child_id) {
        if (absorbed[child_id]) continue;
        if (!can_fuse_transforms(graph, child_id)) continue;

        GraphNodeId parent_id = graph.inputs(child_id)[0];
        if (absorbed[parent_id]) continue;
        if (consumers[parent_id] != 1) continue;

        auto* child_node  = dynamic_cast<TransformNode*>(&graph.node(child_id));
        auto* parent_node = dynamic_cast<TransformNode*>(&graph.node(parent_id));
        if (!child_node || !parent_node) continue;

        // Combine: parent first, then child
        Mat4 combined = parent_node->matrix() * child_node->matrix();
        float combined_opacity = parent_node->opacity() * child_node->opacity();
        child_node->set_matrix(combined);
        child_node->set_opacity(combined_opacity);

        // Rewire: child now takes parent's inputs instead of parent
        const auto& grandparent_inputs = graph.inputs(parent_id);
        graph.disconnect(parent_id, child_id);
        for (GraphNodeId gp_id : grandparent_inputs) {
            graph.connect(gp_id, child_id);
        }

        absorbed[parent_id] = true;
        consumers[parent_id] = 0;
    }

    // Remove absorbed nodes (reverse iteration to avoid ID shifts in the bitmap)
    for (GraphNodeId id = static_cast<GraphNodeId>(node_count) - 1; ; --id) {
        if (absorbed[id]) {
            graph.remove_node(id);
        }
        if (id == 0) break;
    }

    size_t fused_count = 0;
    for (bool a : absorbed) if (a) ++fused_count;
    return fused_count;
}

// ── Branch pruning: remove identity transforms and empty-bbox nodes ─────
size_t prune_branches(RenderGraph& graph, const RenderGraphContext& ctx) {
    const size_t node_count = graph.size();
    if (node_count < 2) return 0;

    std::vector<bool> pruned(node_count, false);
    size_t pruned_count = 0;

    for (GraphNodeId id = 0; id < static_cast<GraphNodeId>(node_count); ++id) {
        if (pruned[id]) continue;
        if (!graph.has_node(id)) continue;
        const auto& node = graph.node(id);

        bool bypassed = false;

        // Identity TransformNode bypass
        if (!bypassed && node.kind() == RenderGraphNodeKind::Transform) {
            auto* transform = dynamic_cast<const TransformNode*>(&node);
            if (transform && transform->is_identity()) {
                size_t consumers = count_consumers(graph, id);
                if (consumers == 1) {
                    for (GraphNodeId n = 0; n < static_cast<GraphNodeId>(node_count); ++n) {
                        if (pruned[n] || !graph.has_node(n)) continue;
                        for (GraphNodeId in : graph.inputs(n)) {
                            if (in == id) {
                                graph.disconnect(id, n);
                                for (GraphNodeId our_in : graph.inputs(id)) {
                                    graph.connect(our_in, n);
                                }
                                pruned[id] = true;
                                ++pruned_count;
                                bypassed = true;
                                break;
                            }
                        }
                        if (bypassed) break;
                    }
                }
            }
        }

        // Nodes with empty predicted bbox — bypass if single-input
        if (!bypassed) {
            auto bbox = node.predicted_bbox(ctx);
            if (bbox && bbox->is_empty()) {
                const auto& our_inputs = graph.inputs(id);
                if (our_inputs.size() == 1) {
                    GraphNodeId bypass_src = our_inputs[0];
                    for (GraphNodeId n = 0; n < static_cast<GraphNodeId>(node_count); ++n) {
                        if (pruned[n] || !graph.has_node(n)) continue;
                        for (GraphNodeId in : graph.inputs(n)) {
                            if (in == id) {
                                graph.disconnect(id, n);
                                if (bypass_src < node_count && !pruned[bypass_src]) {
                                    graph.connect(bypass_src, n);
                                }
                                break;
                            }
                        }
                    }
                }
                pruned[id] = true;
                ++pruned_count;
            }
        }
    }

    // Remove pruned nodes
    for (GraphNodeId id = static_cast<GraphNodeId>(node_count) - 1; ; --id) {
        if (pruned[id]) {
            graph.remove_node(id);
        }
        if (id == 0) break;
    }

    return pruned_count;
}

// ── Count nodes eligible for static bake (those with static inputs and
//     cache policies). The actual baking/persistence happens lazily during
//     first execution via the node cache + PersistentBakeCache.
// ─────────────────────────────────────────────────────────────────────────
size_t count_bake_eligible_nodes(
    RenderGraph&           graph,
    const RenderGraphContext& ctx)
{
    if (!ctx.node_cache) return 0;

    const size_t node_count = graph.size();
    size_t baked = 0;

    for (GraphNodeId id = 0; id < static_cast<GraphNodeId>(node_count); ++id) {
        if (!graph.has_node(id)) continue;
        const auto& node = graph.node(id);

        if (node.frame_dependent()) continue;
        auto policy = node.cache_policy();
        if (!policy.cacheable) continue;

        bool all_inputs_static = true;
        for (GraphNodeId in : graph.inputs(id)) {
            if (in >= node_count || !graph.has_node(in)) continue;
            if (graph.node(in).frame_dependent()) {
                all_inputs_static = false;
                break;
            }
        }

        if (all_inputs_static) ++baked;
    }

    return baked;
}

// ── Main optimization entry point ───────────────────────────────────────
OptimizationResult optimize_graph(
    RenderGraph&           graph,
    const RenderGraphContext& ctx,
    const OptimizationConfig& config)
{
    OptimizationResult result;
    result.nodes_before = graph.size();

    if (config.enable_static_bake) {
        result.static_bakes = count_bake_eligible_nodes(graph, ctx);
    }

    if (config.enable_node_fusion) {
        result.nodes_fused = fuse_nodes(graph);
    }

    if (config.enable_branch_pruning) {
        result.nodes_pruned = prune_branches(graph, ctx);
    }

    result.nodes_after = graph.live_count();
    return result;
}

} // namespace chronon3d::graph::optimizer
