#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
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

    /// Fuse adjacent effect-like nodes (EffectStackNode / AdjustmentNode)
    /// when they share the same frame_dependent policy and the parent has a
    /// single consumer. This eliminates one intermediate framebuffer.
    bool enable_effect_fusion         = true;

    /// Remove graph nodes that are not reachable from the output node.
    /// Run first so subsequent passes operate on a minimal graph.
    bool enable_dead_node_elimination = true;

    /// Experimental descriptor-only ColorMatrix → Opacity → Blend pass.
    /// It is intentionally opt-in until the descriptor is persisted in the
    /// compiled graph and consumed by an executor node. Runtime color fusion
    /// uses the canonical ColorPipeline in the software effect dispatcher.
    bool enable_pixel_fusion         = false;
};

// ── Optimization result ─────────────────────────────────────────────────
struct OptimizationResult {
    size_t nodes_before      = 0;
    size_t nodes_after       = 0;
    size_t nodes_fused       = 0;   // total nodes removed via transform fusion
    size_t nodes_pruned      = 0;   // total nodes removed via pruning
    size_t effects_fused     = 0;   // nodes removed via effect-stack fusion
    size_t dead_nodes_removed = 0;  // unreachable nodes eliminated

    size_t static_bakes      = 0;   // subgraphs eligible for static bake

    // Experimental PixelProgram discovery counters. Candidates are not
    // executable fusion until the compiled graph persists a certified program
    // and the executor consumes it; therefore these remain zero today.
    size_t pixel_fusions     = 0;   // # of certified, executed pixel fusions
    size_t pixel_fusion_bytes_saved = 0;  // measured savings from executed fusions
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

// Removes nodes not reachable from the graph output (purely topological).
// Safe to run before other passes to reduce graph size.
// Returns number of nodes removed.
size_t eliminate_dead_nodes(RenderGraph& graph);

// Fuses adjacent effect-like nodes (EffectStackNode / AdjustmentNode) into a
// single node when safe. Eliminates intermediate framebuffers for multi-step
// post-processing chains.
// Returns number of nodes removed.
size_t fuse_effect_stacks(RenderGraph& graph);

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
