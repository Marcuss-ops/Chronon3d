#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_map>

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
    // visited nodes in.  Hash is FNV-1a (a single deterministic
    // mixing function) so the value is bit-stable across compilers.
    //
    // WP 4.2 — when a `parent_precomp_node` is supplied via compile
    // options, its StableNodeId AND the parent's GraphInstanceId are
    // MIXED INTO the hash so two nested precomp layers using the
    // same composition get distinct graph_instance_ids (this is
    // what enables their SceneProgramStore buckets to be distinct).
    std::vector<StableNodeId> sids;
    sids.reserve(compiled.nodes.size());
    for (size_t i = 0; i < compiled.nodes.size(); ++i) {
        if (compiled.nodes[i].reachable
            && compiled.nodes[i].stable_node_id != kInvalidStableNodeId) {
            sids.push_back(compiled.nodes[i].stable_node_id);
        }
    }
    std::sort(sids.begin(), sids.end());
    // StableNodeId's spaceship default-compares on .value, so sorting
    // is content-stable across compilers and link orders.
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
    // The fast-path requires three conditions simultaneously:
    //   1. options.reuse_if_unchanged_predicate_safe() — today this is
    //      `!options.run_optimizer`; the optimizer cannot be admitted into
    //      the skip path because its effects are not part of the hash
    //      affordance (see FrameGraphCompileOptions).
    //   2. ctx.policy.graph_structure_unchanged — the runtime must have
    //      HONESTLY observed that the new frame's topology matches the
    //      prior frame's topology.  If the application set this flag
    //      speculatively, the hash-mismatch safety below (NIT-3) will
    //      still kick us back to the full path.
    //   3. prior_compiled.structure_hash must equal the freshly
    //      recomputed hash of the new graph.  If they differ, we fall
    //      through to the full path (NIT-3).
    //
    // IMPORTANT: when `run_optimizer == true`, the predicate is gated to
    // `false` for cause #1; consequently `skip_heavy_phases` cannot fire
    // in that mode.  We therefore run the optimizer ONLY inside the
    // fall-through branch below — saving the wasted optimizer traversal
    // when the skip path is going to fire (reviewer Q1).
    const std::uint64_t current_hash =
        compute_structure_hash(graph, compiled.output);
    const bool skip_heavy_phases =
        options.reuse_if_unchanged_predicate_safe()
        && ctx.policy.graph_structure_unchanged
        && prior_compiled.structure_hash == current_hash;

    if (skip_heavy_phases) {
        // Deep-copy topology-derived fields.  These three vectors are
        // exactly the output of the heavy transformer (build_execution_levels,
        // build_node_metadata); copying them preserves every per-call
        // invariant without re-running the topo walk.
        //
        // NOTE: `prior_compiled.lifetimes` is intentionally NOT deep-copied
        // here.  Lifetimes are derived from this-frame's `levels + nodes`
        // (which the skip path re-uses verbatim — OK) BUT their
        // `first_level`/`last_level` and per-input consumer_count are
        // recomputable fall-outs of the topo walk; the always-run
        // post-condition below calls `compute_resource_lifetimes` when
        // `options.compute_lifetimes == true`, which is the canonical
        // recomputation site and the same site used by `compile()`.  The
        // net effect matches compile()'s behavior.  Reviewer Q6.
        compiled.levels         = prior_compiled.levels;
        compiled.nodes          = prior_compiled.nodes;
        compiled.consumer_counts = prior_compiled.consumer_counts;
    } else {
        if (options.run_optimizer) {
            [[maybe_unused]] const auto optimization_result =
                optimizer::optimize_graph(graph, ctx);
        }
        build_execution_levels(graph, compiled.output, compiled);
        build_node_metadata(graph, ctx, compiled, options);
    }

    // ── TICKET-008 / §9.4 — always-run post-conditions (Steps 4 #1–#10) ─────────
    // Sequence mirrors compile() from this point forward so the
    // observed invariants are identical whether the fast-path was
    // taken or the full path ran.

    compiled.graph = std::move(graph);

    if (options.compute_lifetimes) {
        compute_resource_lifetimes(compiled);
    }

    // Always re-derive the structure_hash from the new graph (the affordance
    // other consumers key against).  When the fast-path was taken, this
    // recovers possible subtle hash drift caused by node-insertion-order
    // variance; when the full path ran, this matches build_node_metadata's
    // ordering exactly.
    compiled.structure_hash = compute_structure_hash(compiled.graph, compiled.output);
    compiled.skip_initial_clear = ctx.policy.skip_initial_clear;

    compiled.early_exit_skip.assign(node_count, false);
    for (size_t i = 0; i < std::min(node_count, ctx.node_exec.early_exit_skip.size()); ++i) {
        compiled.early_exit_skip[i] = ctx.node_exec.early_exit_skip[i];
    }

    // Re-derive graph_instance_id (sorted-stable_node-id FNV-1a mix).
    // Always — this is the canary against the "names changed but topology
    // didn't" edge case (TICKET Known Limitation): even on the skip
    // path the prior's `.nodes[i].stable_node_id` may be stale relative
    // to the new graph's name set; the graph-level hash deflates that
    // staleness at the bucket key level.  Per-node `stable_node_id` is
    // NOT re-derived (see doc-comment on `compile_with_reuse`).
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

                // Canonical cache surface — derived views (frame_dependent / cacheable /
                // disk_cacheable) on CompiledNodeInfo were removed; read this directly.
                node_info.cache_policy = node.cache_policy();

                if (id < ctx.node_exec.early_exit_skip.size() && ctx.node_exec.early_exit_skip[id]) {
                    node_info.early_exit_skip = true;
                }
                if (options.compute_bboxes) {
                    node_info.predicted_bbox = node.predicted_bbox(ctx);
                }

                // ── Work Package 4 — derive stable_node_id (PR 4.1/4.2/4.3) ──
                // Inputs: deterministic strings (layer_id, name) + a kind enum.
                // EXCLUDES: raw pointers, timestamps, transient counters.
                // WP 4.1 — switch from `std::hash<std::string>` (compiler-
                // dependent) to XXH64 via `hash_string` so identical inputs
                // produce identical outputs across compilers / libstdc++
                // versions — required for cache round-trip safety (PR 4.4).
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

    // ── Work Package 4.4 — input-aware refinement (PR 6.9) ───────────────────
    // Two nodes that share (layer_id, kind, name) but have different
    // inputs (e.g. multiple CompositeNodes in a root-level compositing
    // chain) would collide under the preliminary hash above.  Fold each
    // reachable node's input stable_node_ids into its own hash, processing
    // in topological order so upstream nodes are already finalised.
    //
    // Leaf nodes (zero inputs) keep their preliminary SID unchanged.
    // This is a Merkle-style refinement: the SID becomes a content hash
    // of the node's local identity XOR its reachable subgraph.
    for (const auto& level : compiled.levels) {
        for (GraphNodeId id : level) {
            if (id >= node_count) continue;
            auto& node_info = compiled.nodes[id];
            if (!node_info.reachable) continue;
            if (node_info.stable_node_id == kInvalidStableNodeId) continue;

            uint64_t h = node_info.stable_node_id.value;
            bool folded = false;

            // Collect reachable input SIDs and sort for determinism —
            // graph builder insertion order is not guaranteed stable
            // across builds (same as compile()'s sorted-SID contract for
            // graph_instance_id).
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
                // FNV-1a fold — same mixing as hash_stable_node_inputs
                h ^= sid;
                h *= 0x100000001b3ULL;
                folded = true;
            }
            if (folded) {
                node_info.stable_node_id = StableNodeId{h == 0u ? 1u : h};
            }
        }
    }

    // ── Work Package 4 — collision detection (PR 4.3) ────────────────────────
    // Two distinct reachable nodes in the same graph MUST NOT produce
    // identical stable_node_ids.  Detect this and throw so the bug is
    // surfaced at compile time (an unconstrained collision would later
    // produce a catastrophic cache aliasing in `SceneProgramStore`).
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
