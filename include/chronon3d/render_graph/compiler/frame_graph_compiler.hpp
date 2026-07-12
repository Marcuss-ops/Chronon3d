#pragma once

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compile_options.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>

namespace chronon3d::graph {

class FrameGraphCompiler {
public:
    [[nodiscard]] CompiledFrameGraph compile(
        RenderGraph graph,
        RenderGraphContext& ctx,
        const FrameGraphCompileOptions& options = {}
    ) const;

    // ── TICKET-008 / refactor-roadmap §9.4 closure ───────────────────────────
    // Compile a NEW `RenderGraph` against a `prior_compiled` for the prior
    // frame.  When `ctx.policy.graph_structure_unchanged == true` AND the
    // freshly-recomputed `compute_structure_hash(graph, output)` matches
    // `prior_compiled.structure_hash` AND
    // `options.reuse_if_unchanged_predicate_safe()` returns true (i.e.
    // `options.run_optimizer == false`), the heavy phases
    // (`build_execution_levels`, `build_node_metadata`) are SKIPPED and
    // the topology-derived fields are deep-copied from `prior_compiled`.
    //
    // Skip-payload (deep-copied from prior): `levels`, `nodes`,
    // `consumer_counts`.
    //
    // Always-run post-conditions (in order):
    //   1. `compiled.graph = std::move(graph)` — consume input graph.
    //   2. `compiled.output = graph.output()` — preserved.
    //   3. If `options.compute_lifetimes` — `compute_resource_lifetimes`
    //      (always recompute; the prior's lifetimes may not cover the
    //      current `early_exit_skip` overlay).
    //   4. `compiled.structure_hash = compute_structure_hash(...)`
    //      (always re-derive; this is the affordance other consumers key
    //      against).
    //   5. `compiled.skip_initial_clear = ctx.policy.skip_initial_clear`.
    //   6. `compiled.early_exit_skip = ctx.node_exec.early_exit_skip`
    //      (per-node mask, policy-/frame-time-dependent).
    //   7. `compiled.graph_instance_id` re-derived via FNV-1a over the
    //      sorted reachable `stable_node_id` set (defensive against the
    //      "names changed but topology didn't" edge case — see Known
    //      Limitation below).
    //   8. If `options.validate_dag` — `validate(compiled)`.
    //   9. `compiled.valid = true`.
    //
    // Hash-mismatch safety (NIT-3): when `prior_compiled.structure_hash`
    // differs from the freshly recomputed hash, this overload MUST fall
    // through to the standard full compile path.  No silent staleness.
    //
    // Run-optimizer safety (TICKET-008 Step 3): when
    // `options.run_optimizer == true`, the prior's optimization state is
    // unknown to compile()-internal logic, so the affordance MIGHT be
    // unsafe.  The skip predicate is therefore gated on `!run_optimizer`
    // (see `reuse_if_unchanged_predicate_safe()`).
    //
    // KNOWN LIMITATION: `compute_structure_hash` hashes node kind + input
    // ids + output — it does NOT hash node names + layer_ids.  Two
    // graphs with the same topology but DIFFERENT node names produce the
    // same `structure_hash` and the SKIP path returns the prior's
    // `nodes[]` array — whose `stable_node_id` field reflects the PRIOR
    // names, not the NEW ones.  `compiled.structure_hash` and
    // `compiled.graph_instance_id` are re-derived so the graph-level
    // identity hash still reflects the new names, but per-node
    // `stable_node_id` is NOT.  Callers that rely on
    // `compiled.nodes[id].stable_node_id` across the reuse boundary
    // must keep node names stable OR fall through (either by renaming
    // both graphs identically, or by leaving `graph_structure_unchanged`
    // false when names change).
    //
    // See TICKET-008 in docs/FOLLOWUP_TICKETS.md and refactor-roadmap
    // §9.4 closure-note sub-sections (Skip-safety constraints +
    // Affordance attribution) for the originating contract.
    [[nodiscard]] CompiledFrameGraph compile_with_reuse(
        RenderGraph graph,
        RenderGraphContext& ctx,
        const CompiledFrameGraph& prior_compiled,
        const FrameGraphCompileOptions& options = {}
    ) const;

    [[nodiscard]] static std::uint64_t compute_structure_hash(
        const RenderGraph& graph,
        GraphNodeId output
    );

private:
    void build_execution_levels(
        RenderGraph& graph,
        GraphNodeId output,
        CompiledFrameGraph& compiled
    ) const;

    void build_node_metadata(
        RenderGraph& graph,
        RenderGraphContext& ctx,
        CompiledFrameGraph& compiled,
        const FrameGraphCompileOptions& options
    ) const;

    void compute_resource_lifetimes(
        CompiledFrameGraph& compiled
    ) const;

    void validate(
        const CompiledFrameGraph& compiled
    ) const;
};

} // namespace chronon3d::graph
