#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <algorithm>
#include <stack>
#include <unordered_set>

namespace chronon3d::graph::optimizer {

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

// ── Helper: build consumer counts vector ────────────────────────────────
static std::vector<size_t> build_consumer_counts(const RenderGraph& graph) {
    const size_t n = graph.size();
    std::vector<size_t> consumers(n, 0);
    for (GraphNodeId id = 0; id < static_cast<GraphNodeId>(n); ++id) {
        if (!graph.has_node(id)) continue;
        for (GraphNodeId in : graph.inputs(id)) {
            if (in < n) consumers[in]++;
        }
    }
    return consumers;
}

static void remove_marked_nodes(RenderGraph& graph, const std::vector<bool>& marked) {
    if (marked.empty()) return;
    for (GraphNodeId id = static_cast<GraphNodeId>(marked.size()) - 1; ; --id) {
        if (marked[id]) {
            graph.remove_node(id);
        }
        if (id == 0) break;
    }
}

static void rewire_child_to_grandparents(RenderGraph& graph, GraphNodeId parent_id, GraphNodeId child_id) {
    const auto grandparents = graph.inputs(parent_id);
    graph.disconnect(parent_id, child_id);
    for (GraphNodeId gp_id : grandparents) {
        graph.connect(gp_id, child_id);
    }
}

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

static bool is_effect_like(RenderGraphNodeKind kind) {
    return kind == RenderGraphNodeKind::Effect || kind == RenderGraphNodeKind::Adjustment;
}

static EffectStack* effect_stack_mut(RenderGraphNode& node) {
    if (auto* effect = dynamic_cast<EffectStackNode*>(&node)) {
        return &effect->effects();
    }
    if (auto* adjustment = dynamic_cast<AdjustmentNode*>(&node)) {
        return &adjustment->effects();
    }
    return nullptr;
}

// ── Pass: eliminate unreachable nodes ───────────────────────────────────
// Removes all nodes that cannot be reached from the graph's output node.
// This is a purely topological pass with no semantic knowledge; it is safe
// to run first so that subsequent passes operate on a minimal graph.
size_t eliminate_dead_nodes(RenderGraph& graph) {
    if (!graph.has_output()) return 0;

    const size_t node_count = graph.size();
    std::vector<bool> reachable(node_count, false);
    std::vector<GraphNodeId> stack{graph.output()};

    while (!stack.empty()) {
        GraphNodeId id = stack.back();
        stack.pop_back();
        if (id >= node_count || reachable[id] || !graph.has_node(id)) continue;
        reachable[id] = true;
        for (GraphNodeId in : graph.inputs(id)) {
            stack.push_back(in);
        }
    }

    size_t removed = 0;
    std::vector<bool> remove(node_count, false);
    for (GraphNodeId id = 0; id < static_cast<GraphNodeId>(node_count); ++id) {
        if (graph.has_node(id) && !reachable[id]) {
            remove[id] = true;
            ++removed;
        }
    }
    remove_marked_nodes(graph, remove);
    return removed;
}

// ── Pass: fuse adjacent effect-like nodes ────────────────────────────────
// When two effect-like nodes (EffectStackNode or AdjustmentNode) are chained
// parent → child with a single consumer on the parent and matching
// frame_dependent, the parent's effect list is prepended into the child and
// the parent is removed.
// This eliminates one intermediate framebuffer per fused pair.
size_t fuse_effect_stacks(RenderGraph& graph) {
    const size_t node_count = graph.size();
    if (node_count < 2) return 0;

    std::vector<bool> absorbed(node_count, false);
    auto consumers = build_consumer_counts(graph);

    for (GraphNodeId child_id = 0; child_id < static_cast<GraphNodeId>(node_count); ++child_id) {
        if (absorbed[child_id]) continue;
        if (!graph.has_node(child_id)) continue;

        auto& child_node = graph.node(child_id);
        if (!is_effect_like(child_node.kind())) continue;

        auto* child_effect = effect_stack_mut(child_node);
        if (!child_effect) continue;

        const auto& inputs = graph.inputs(child_id);
        if (inputs.size() != 1) continue;

        GraphNodeId parent_id = inputs[0];
        if (absorbed[parent_id]) continue;
        if (!graph.has_node(parent_id)) continue;

        auto& parent_node = graph.node(parent_id);
        if (!is_effect_like(parent_node.kind())) continue;

        auto* parent_effect = effect_stack_mut(parent_node);
        if (!parent_effect) continue;

        // Must share the same frame_dependent policy
        if (parent_node.frame_dependent() != child_node.frame_dependent()) continue;

        // Only safe to absorb if parent has exactly 1 consumer
        if (consumers[parent_id] != 1) continue;

        // Merge: parent effects run first, so prepend them before child's effects
        child_effect->insert(child_effect->begin(), parent_effect->begin(), parent_effect->end());

        // Rewire: child adopts parent's inputs
        rewire_child_to_grandparents(graph, parent_id, child_id);

        absorbed[parent_id] = true;
        consumers[parent_id] = 0;
    }

    remove_marked_nodes(graph, absorbed);

    size_t fused = 0;
    for (bool a : absorbed) if (a) ++fused;
    return fused;
}

// ── Pass: fuse adjacent Transform nodes ─────────────────────────────────
size_t fuse_nodes(RenderGraph& graph) {
    const size_t node_count = graph.size();
    if (node_count < 2) return 0;

    std::vector<bool> absorbed(node_count, false);
    auto consumers = build_consumer_counts(graph);

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
        rewire_child_to_grandparents(graph, parent_id, child_id);

        absorbed[parent_id] = true;
        consumers[parent_id] = 0;
    }

    remove_marked_nodes(graph, absorbed);

    size_t fused_count = 0;
    for (bool a : absorbed) if (a) ++fused_count;
    return fused_count;
}

// ── Pass: branch pruning ─────────────────────────────────────────────────
// Removes identity transforms and nodes with empty predicted bbox.
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
                if (consumers == 1 && graph.inputs(id).size() == 1) {
                    for (GraphNodeId n = 0; n < static_cast<GraphNodeId>(node_count); ++n) {
                        if (pruned[n] || !graph.has_node(n)) continue;
                        for (GraphNodeId in : graph.inputs(n)) {
                            if (in == id) {
                                graph.replace_input(n, id, graph.inputs(id)[0]);
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
            auto bbox = node.predicted_bbox(ctx, {});
            if (bbox && bbox->is_empty()) {
                const auto& our_inputs = graph.inputs(id);
                if (our_inputs.size() == 1) {
                    GraphNodeId bypass_src = our_inputs[0];
                    for (GraphNodeId n = 0; n < static_cast<GraphNodeId>(node_count); ++n) {
                        if (pruned[n] || !graph.has_node(n)) continue;
                        for (GraphNodeId in : graph.inputs(n)) {
                            if (in == id) {
                                graph.replace_input(n, id, bypass_src);
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

    remove_marked_nodes(graph, pruned);

    return pruned_count;
}

// ── Pass: count static bake eligible nodes ───────────────────────────────
// The actual baking/persistence happens lazily during first execution via
// the node cache + PersistentBakeCache.
size_t count_bake_eligible_nodes(
    RenderGraph&           graph,
    const RenderGraphContext& ctx)
{
    if (!ctx.resources.node_cache) return 0;

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
// Pass order:
//  1. eliminate_dead_nodes  — reduce graph to reachable nodes only
//  2. count_bake_eligible   — read-only, no mutations
//  3. fuse_effect_stacks    — merge consecutive EffectStackNode pairs
//  4. fuse_nodes            — merge consecutive TransformNode pairs
//  5. prune_branches        — remove identity nodes + empty-bbox subtrees
OptimizationResult optimize_graph(
    RenderGraph&           graph,
    const RenderGraphContext& ctx,
    const OptimizationConfig& config)
{
    OptimizationResult result;
    result.nodes_before = graph.size();

    if (config.enable_dead_node_elimination) {
        result.dead_nodes_removed = eliminate_dead_nodes(graph);
    }

    if (config.enable_static_bake) {
        result.static_bakes = count_bake_eligible_nodes(graph, ctx);
    }

    if (config.enable_effect_fusion) {
        result.effects_fused = fuse_effect_stacks(graph);
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
