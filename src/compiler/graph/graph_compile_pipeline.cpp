// ═══════════════════════════════════════════════════════════════════════════
// graph_compile_pipeline.cpp — compiler-owned graph lowering pipeline
// ═══════════════════════════════════════════════════════════════════════════
//
// Build phases live in graph_build_pipeline.cpp. This TU hosts the compiler
// compile surfaces and the canonical topology hash.

#include <chronon3d/compiler/detail/graph_compile_pipeline.hpp>
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/render_graph/nodes/clip_transition_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <algorithm>
#include <functional>
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace chronon3d::graph {
namespace {

// Optimization is advisory: it runs in-place on the graph and cannot fail, so
// the [[nodiscard]] OptimizationResult carries fusion/pruning census only.
// Surface it under the diagnostics policy instead of discarding it, so the
// counters the optimizer was built to provide stay observable.
void report_optimization_result(
    const optimizer::OptimizationResult& result, const RenderGraphContext& ctx) {
    if (!ctx.policy.diagnostics_enabled) return;
    spdlog::debug(
        "[graph-optimizer] nodes {} -> {} ({} fused, {} pruned, {} dead, "
        "{} effect-fused, {} static-bakes)",
        result.nodes_before, result.nodes_after, result.nodes_fused,
        result.nodes_pruned, result.dead_nodes_removed, result.effects_fused,
        result.static_bakes);
}

// Standalone compiler callers may wire only RenderGraphContext::backend.
// Capture the backend-owned immutable processor snapshot at this boundary so
// every compile entry point observes the same ownership contract as the
// production pipeline wiring.
void capture_processor_contract(RenderGraphContext& ctx) {
    if (!ctx.backend) return;

    if (!ctx.node_exec.processor_snapshot) {
        ctx.node_exec.processor_snapshot = ctx.backend->processor_snapshot();
    }
    ctx.processor_snapshot_required =
        ctx.processor_snapshot_required ||
        ctx.backend->requires_processor_snapshot();
    if (ctx.registry_generation == 0 && ctx.node_exec.processor_snapshot) {
        ctx.registry_generation = ctx.node_exec.processor_snapshot->generation();
    }
}

} // namespace

CompiledFrameGraph GraphCompilePipeline::compile(
    RenderGraph graph,
    RenderGraphContext& ctx,
    const FrameGraphCompileOptions& options
) const {
    capture_processor_contract(ctx);
    if (options.run_optimizer) {
        const auto optimization_result = optimizer::optimize_graph(graph, ctx);
        report_optimization_result(optimization_result, ctx);
    }

    CompiledFrameGraph compiled;
    compiled.registry_generation = ctx.registry_generation;
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

    validate_renderable_graph(graph, compiled.output, ctx);
    build_execution_levels(graph, compiled.output, compiled);
    build_node_metadata(graph, ctx, compiled, options);
    compute_cache_key_requirement(compiled);

    // P1.0: level dumps are diagnostics-only, not per-compile hot logging.
    if (ctx.policy.diagnostics_enabled) {
        for (size_t l = 0; l < compiled.levels.size(); ++l) {
            for (GraphNodeId id : compiled.levels[l]) {
                spdlog::info("[compiled_level] level={} id={} node='{}'", l, id, graph.node(id).name());
            }
        }
    }

    // Resource compilation operates on the finalized graph topology.
    compiled.graph = std::move(graph);

    if (options.compute_lifetimes) {
        build_compiled_resource_table(compiled, ctx);
    }

    compiled.structure_hash = compute_structure_hash(
        compiled.graph, compiled.output, ctx.registry_generation);
    compiled.skip_initial_clear = ctx.policy.skip_initial_clear;

    compiled.early_exit_skip.assign(node_count, false);
    for (size_t i = 0; i < std::min(node_count, ctx.node_exec.early_exit_skip.size()); ++i) {
        compiled.early_exit_skip[i] = ctx.node_exec.early_exit_skip[i];
    }

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

void GraphCompilePipeline::compute_cache_key_requirement(
    CompiledFrameGraph& compiled
) const {
    // Base set: nodes that can perform a cache lookup in some frame.  This
    // mirrors the STATIC half of evaluate_cache's use_cache decision
    // (policy.enabled() && !policy.frame_dependent(), minus the forced
    // Composite bypass).  The dynamic half (inputs_frame_dependent,
    // node_cache presence, native-video surfaces) can only turn a lookup
    // OFF, never ON, so the static superset is the correct conservative
    // seed.  The executor ORs in telemetry key consumption afterwards.
    const size_t node_count = compiled.nodes.size();
    for (auto& info : compiled.nodes) {
        info.cache_key_required =
            info.reachable &&
            info.cache_policy.enabled() &&
            !info.cache_policy.frame_dependent() &&
            info.kind != RenderGraphNodeKind::Composite;
    }

    // Close over consumers: resolve_inputs folds a producer's
    // resolved_key_digest into every consumer's input_hash, so any producer
    // feeding a required consumer is itself required.  compiled.levels is
    // topologically ordered (producers in earlier levels), therefore a
    // reverse sweep propagates the requirement upstream in one pass.
    for (size_t l = compiled.levels.size(); l-- > 0;) {
        for (GraphNodeId id : compiled.levels[l]) {
            if (id >= node_count || !compiled.nodes[id].cache_key_required) {
                continue;
            }
            for (GraphNodeId parent : compiled.nodes[id].inputs) {
                if (parent < node_count && compiled.nodes[parent].reachable) {
                    compiled.nodes[parent].cache_key_required = true;
                }
            }
        }
    }
}

std::uint64_t GraphCompilePipeline::compute_structure_hash(
    const RenderGraph& graph,
    GraphNodeId output,
    std::uint64_t registry_generation
) {
    uint64_t sig = hash_string("chronon.compiled-topology.v2");
    sig = hash_combine(sig, hash_value(registry_generation));
    sig = hash_combine(sig, hash_value(graph.size()));
    for (GraphNodeId id = 0; id < graph.size(); ++id) {
        if (!graph.has_node(id)) continue;
        const auto& node = graph.node(id);
        sig = hash_combine(sig, hash_value(id));
        sig = hash_combine(sig, hash_value(static_cast<int>(node.kind())));
        sig = hash_combine(sig, hash_string(node.name()));
        sig = hash_combine(sig, hash_string(node.layer_id()));
        sig = hash_combine(sig, hash_value(static_cast<int>(node.cache_policy().mode)));
        sig = hash_combine(sig, hash_value(static_cast<int>(node.cache_policy().invalidation)));

        if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
            sig = hash_combine(sig, hash_string("processor.source"));
            sig = hash_combine(sig, hash_value(static_cast<int>(source->render_node().shape.type())));
        } else if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
            sig = hash_combine(sig, hash_string("processor.multi_source"));
            sig = hash_combine(sig, hash_value(multi->items().size()));
            for (const auto& item : multi->items()) {
                sig = hash_combine(sig, item.node
                    ? hash_value(static_cast<int>(item.node->shape.type()))
                    : hash_string("null-item"));
            }
        } else if (const auto* effect = dynamic_cast<const EffectStackNode*>(&node)) {
            sig = hash_combine(sig, hash_string("processor.effect_stack"));
            for (const auto& instance : effect->effects()) {
                if (!instance.enabled) continue;
                sig = hash_combine(sig, hash_string(instance.descriptor.id));
                sig = hash_combine(sig, hash_value(static_cast<int>(instance.effect_type)));
            }
        } else if (const auto* adjustment = dynamic_cast<const AdjustmentNode*>(&node)) {
            sig = hash_combine(sig, hash_string("processor.adjustment"));
            for (const auto& instance : adjustment->effects()) {
                if (!instance.enabled) continue;
                sig = hash_combine(sig, hash_string(instance.descriptor.id));
                sig = hash_combine(sig, hash_value(static_cast<int>(instance.effect_type)));
            }
        } else if (dynamic_cast<const TransitionNode*>(&node)) {
            sig = hash_combine(sig, hash_string("processor.transition"));
            sig = hash_combine(sig, hash_string(node.name()));
        } else if (dynamic_cast<const ClipTransitionNode*>(&node)) {
            sig = hash_combine(sig, hash_string("processor.clip_transition"));
            sig = hash_combine(sig, hash_string(node.name()));
        } else {
            sig = hash_combine(sig, hash_string("processor."));
        }

        const auto& inputs = graph.inputs(id);
        sig = hash_combine(sig, hash_value(inputs.size()));
        for (GraphNodeId input : inputs) {
            sig = hash_combine(sig, hash_value(input));
        }
    }
    sig = hash_combine(sig, hash_string("output"));
    sig = hash_combine(sig, hash_value(output));
    return sig;
}

CompiledFrameGraph GraphCompilePipeline::compile_with_reuse(
    RenderGraph graph,
    RenderGraphContext& ctx,
    const CompiledFrameGraph& prior_compiled,
    const FrameGraphCompileOptions& options
) const {
    capture_processor_contract(ctx);
    CompiledFrameGraph compiled;
    compiled.registry_generation = ctx.registry_generation;
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

    const std::uint64_t current_hash =
        compute_structure_hash(
            graph, compiled.output, ctx.registry_generation);
    const auto current_snapshot = ctx.node_exec.processor_snapshot;
    const auto current_snapshot_identity = current_snapshot
        ? current_snapshot->identity()
        : 0;
    const bool snapshot_identity_unchanged =
        prior_compiled.processor_snapshot_identity == current_snapshot_identity;
    const bool skip_heavy_phases =
        options.reuse_if_unchanged_predicate_safe()
        && ctx.policy.graph_structure_unchanged
        && prior_compiled.structure_hash == current_hash
        && snapshot_identity_unchanged;

    validate_renderable_graph(graph, compiled.output, ctx);

    if (skip_heavy_phases) {
        compiled.levels                  = prior_compiled.levels;
        compiled.nodes                   = prior_compiled.nodes;
        compiled.processor_snapshot      = prior_compiled.processor_snapshot;
        compiled.processor_snapshot_identity =
            prior_compiled.processor_snapshot_identity;
        compiled.shape_processor_table   = prior_compiled.shape_processor_table;
        compiled.effect_processor_table  = prior_compiled.effect_processor_table;
    } else {
        if (options.run_optimizer) {
            const auto optimization_result = optimizer::optimize_graph(graph, ctx);
            report_optimization_result(optimization_result, ctx);
            compiled.output = graph.output();
        }
        build_execution_levels(graph, compiled.output, compiled);
        build_node_metadata(graph, ctx, compiled, options);
        compute_cache_key_requirement(compiled);
    }

    compiled.graph = std::move(graph);

    if (options.compute_lifetimes) {
        build_compiled_resource_table(compiled, ctx);
    }

    compiled.structure_hash = compute_structure_hash(
        compiled.graph, compiled.output, ctx.registry_generation);
    compiled.skip_initial_clear = ctx.policy.skip_initial_clear;

    compiled.early_exit_skip.assign(node_count, false);
    for (size_t i = 0; i < std::min(node_count, ctx.node_exec.early_exit_skip.size()); ++i) {
        compiled.early_exit_skip[i] = ctx.node_exec.early_exit_skip[i];
    }

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
