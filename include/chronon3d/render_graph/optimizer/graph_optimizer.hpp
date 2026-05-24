#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <cstddef>
#include <vector>
#include <string>

namespace chronon3d::graph::optimizer {

// ── Optimization configuration ──────────────────────────────────────────
struct OptimizationConfig {
    bool enable_node_fusion     = true;
    bool enable_branch_pruning  = true;
    bool enable_static_bake     = true;   // count static subgraph bake opportunities
};

// ── Optimization result ─────────────────────────────────────────────────
struct OptimizationResult {
    size_t nodes_before      = 0;
    size_t nodes_after       = 0;
    size_t nodes_fused       = 0;   // total nodes removed via fusion
    size_t nodes_pruned      = 0;   // total nodes removed via pruning

    size_t static_bakes      = 0;   // subgraphs baked to cache
};

// ── Main optimization entry point ───────────────────────────────────────
// Runs all enabled optimization passes on the graph in-place.
// Returns a summary of what was optimized.
[[nodiscard]] OptimizationResult optimize_graph(
    RenderGraph&           graph,
    const RenderGraphContext& ctx,
    const OptimizationConfig& config = {}
);

// ── Individual pass entry points (exposed for testing) ──────────────────

// Fuses adjacent nodes of compatible types (Transform+Transform, Effect+Effect).
// Returns number of nodes removed.
size_t fuse_nodes(RenderGraph& graph);

// Prunes dead branches (no-op identity transforms, empty bbox subtrees).
// Returns number of nodes removed.
size_t prune_branches(RenderGraph& graph, const RenderGraphContext& ctx);

// Counts nodes eligible for static bake (all inputs frame-invariant + cacheable).
// The actual baking happens lazily during first execution via node cache.
// Returns number of eligible nodes.
size_t count_bake_eligible_nodes(
    RenderGraph&           graph,
    const RenderGraphContext& ctx
);

} // namespace chronon3d::graph::optimizer
