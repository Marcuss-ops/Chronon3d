// ═══════════════════════════════════════════════════════════════════════════
// frame_graph_compiler.cpp — public compile surface (FASE 16)
// ═══════════════════════════════════════════════════════════════════════════
//
// FASE 16 — extraction completed: build_execution_levels(),
// build_node_metadata(), compute_resource_lifetimes(), validate()
// moved to frame_graph_builder.cpp.  This TU now hosts ONLY the
// public API surface: compile(), compile_with_reuse(), and
// compute_structure_hash().

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace chronon3d::graph {

CompiledFrameGraph FrameGraphCompiler::compile(
    RenderGraph graph,
    RenderGraphContext& ctx,
    const FrameGraphCompileOptions& options
) const {
    if (options.run_optimizer) {
        [[maybe_unused]] const auto optimization_result =
            optimizer::optimize_graph(graph, ctx);
    }

    CompiledFrameGraph compiled;
    if (!graph.has_output()) {
        compiled.valid = false;
        compiled.graph = std::move(graph);
        return compiled;
    }

    compiled.output = graph.output();
    const size_t node_count = graph.size();

    if (node_count == 0 || compiled.output == k_invalid_node || compiled.output >= node_count) {
        compiled.valid = false;
        compiled.graph = std::move(graph);
        return compiled;
    }

    build_execution_levels(graph, compiled.output, compiled);
    build_node_metadata(graph, ctx, compiled, options);

    // Temp graph assignment to allow lifetime calculation
    compiled.graph = std::move(graph);

    if (options.compute_lifetimes) {
        compute_resource_lifetimes(compiled);
    }

    compiled.structure_hash = compute_structure_hash(compiled.graph, compiled.output);
    compiled.skip_initial_clear = ctx.policy.skip_initial_clear;

    compiled.early_exit_skip.assign(node_count, false);
    for (size_t i = 0; i < std::min(node_count, ctx.node_exec.early_exit_skip.size()); ++i) {
        compiled.early_exit_skip[i] = ctx.node_exec.early_exit_skip[i];
    }

    // ── Work Package 4 — graph instance id (WP 4.0/4.1) ────────────────
    // Hash the SORTED SET of reachable stable_node_ids (excluding the
    // invalid sentinel) so a re-build of the same source topology
    // yields the same id regardless of which order the compiler
    // visited nodes in.
    std::vector<StableNodeId> sids;
    sids.reserve(compiled.nodes.size());
    for (size_t i = 0; i < compiled.nodes.size(); ++i) {
        if (compiled.nodes[i].reachable
            && compiled.nodes[i].stable_node_id != kInvalidStableNodeId) {
            sids.push_back(compiled.nodes[i].stable_node_id);
        }
    }
    std::sort(sids.begin(), sids.end());
    constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime    = 0x100000001b3ULL;
    std::uint64_t h = kOffsetBasis;
    // WP 4.2 — fold parent scope first so two precomps using the same
    // composition (same sorted SIDs below) collide IF AND ONLY IF
    // they share the same parent precomp stable node + parent graph.
    if (options.parent_precomp_node != kInvalidStableNodeId) {
        h ^= options.parent_graph_instance.value;
        h *= kFnvPrime;
        h ^= options.parent_precomp_node.value;
        h *= kFnvPrime;
    }
    for (StableNodeId sid : sids) {
        h ^= sid.value;
        h *= kFnvPrime;
    }
    compiled.graph_instance_id =
        GraphInstanceId{h == 0u ? 1u : h};

    if (options.validate_dag) {
        validate(compiled);
    }

    compiled.valid = true;

    return compiled;
}

std::uint64_t FrameGraphCompiler::compute_structure_hash(
    const RenderGraph& graph,
    GraphNodeId output
) {
    uint64_t sig = hash_value(graph.size());
    for (GraphNodeId id = 0; id < graph.size(); ++id) {
        if (!graph.has_node(id)) continue;
        sig = hash_combine(sig, hash_value(static_cast<int>(graph.node(id).kind())));
        const auto& inputs = graph.inputs(id);
        sig = hash_combine(sig, hash_value(inputs.size()));
        for (GraphNodeId input : inputs) {
            sig = hash_combine(sig, hash_value(input));
        }
    }
    sig = hash_combine(sig, hash_value(output));
    return sig;
}

CompiledFrameGraph FrameGraphCompiler::compile_with_reuse(
    RenderGraph graph,
    RenderGraphContext& ctx,
    const CompiledFrameGraph& prior_compiled,
    const FrameGraphCompileOptions& options
) const {
    CompiledFrameGraph compiled;
    if (!graph.has_output()) {
        compiled.valid = false;
        compiled.graph = std::move(graph);
        return compiled;
    }

    compiled.output = graph.output();
    const size_t node_count = graph.size();

    if (node_count == 0 || compiled.output == k_invalid_node || compiled.output >= node_count) {
        compiled.valid = false;
        compiled.graph = std::move(graph);
        return compiled;
    }

    // ── TICKET-008 / §9.4 — skip predicate ──────────────────────────────────
    const std::uint64_t current_hash =
        compute_structure_hash(graph, compiled.output);
    const bool skip_heavy_phases =
        options.reuse_if_unchanged_predicate_safe()
        && ctx.policy.graph_structure_unchanged
        && prior_compiled.structure_hash == current_hash;

    if (skip_heavy_phases) {
        // Deep-copy topology-derived fields.
        compiled.levels         = prior_compiled.levels;
        compiled.nodes          = prior_compiled.nodes;
        compiled.consumer_counts = prior_compiled.consumer_counts;
    } else {
        if (options.run_optimizer) {
            [[maybe_unused]] const auto optimization_result =
                optimizer::optimize_graph(graph, ctx);
            // The optimizer may remove/rewrite the output node.  Re-read it
            // before rebuilding levels; retaining the pre-optimization id
            // can address a removed node on the reuse fall-through path.
            compiled.output = graph.output();
        }
        build_execution_levels(graph, compiled.output, compiled);
        build_node_metadata(graph, ctx, compiled, options);
    }

    // ── Always-run post-conditions ─────────
    compiled.graph = std::move(graph);

    if (options.compute_lifetimes) {
        compute_resource_lifetimes(compiled);
    }

    compiled.structure_hash = compute_structure_hash(compiled.graph, compiled.output);
    compiled.skip_initial_clear = ctx.policy.skip_initial_clear;

    compiled.early_exit_skip.assign(node_count, false);
    for (size_t i = 0; i < std::min(node_count, ctx.node_exec.early_exit_skip.size()); ++i) {
        compiled.early_exit_skip[i] = ctx.node_exec.early_exit_skip[i];
    }

    // Re-derive graph_instance_id (sorted-stable_node-id FNV-1a mix).
    std::vector<StableNodeId> sids;
    sids.reserve(compiled.nodes.size());
    for (size_t i = 0; i < compiled.nodes.size(); ++i) {
        if (compiled.nodes[i].reachable
            && compiled.nodes[i].stable_node_id != kInvalidStableNodeId) {
            sids.push_back(compiled.nodes[i].stable_node_id);
        }
    }
    std::sort(sids.begin(), sids.end());
    constexpr std::uint64_t kOffsetBasis2 = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime2    = 0x100000001b3ULL;
    std::uint64_t h2 = kOffsetBasis2;
    if (options.parent_precomp_node != kInvalidStableNodeId) {
        h2 ^= options.parent_graph_instance.value;
        h2 *= kFnvPrime2;
        h2 ^= options.parent_precomp_node.value;
        h2 *= kFnvPrime2;
    }
    for (StableNodeId sid : sids) {
        h2 ^= sid.value;
        h2 *= kFnvPrime2;
    }
    compiled.graph_instance_id =
        GraphInstanceId{h2 == 0u ? 1u : h2};

    if (options.validate_dag) {
        validate(compiled);
    }

    compiled.valid = true;

    return compiled;
}

} // namespace chronon3d::graph
