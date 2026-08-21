#include "graph_cache_coordinator.hpp"

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>
#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/render_graph/pipeline/pipeline_catalogs.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/trace_categories.hpp>
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/core/enum_utils.hpp>
#include "../builder/graph_builder_internal.hpp"
#include "../builder/graph_builder_pipeline.hpp"
#include <spdlog/spdlog.h>
#include <exception>
#include <stdexcept>

namespace chronon3d::graph {

namespace {

/// Owns a checked-out cache entry until the coordinator either publishes a
/// refreshed candidate or restores the original entry. This keeps every
/// failure path exception-safe: a fresh compile or processor-resolution error
/// can never leave the previous cache slot empty.
class CachedGraphLease {
public:
    CachedGraphLease(
        CompiledGraphCache& cache,
        std::optional<CompiledFrameGraph> candidate,
        int width,
        int height)
        : cache_(cache)
        , candidate_(std::move(candidate))
        , width_(width)
        , height_(height) {}

    CachedGraphLease(const CachedGraphLease&) = delete;
    CachedGraphLease& operator=(const CachedGraphLease&) = delete;

    ~CachedGraphLease() noexcept {
        if (!candidate_) return;
        try {
            cache_.store(std::move(*candidate_), width_, height_);
        } catch (...) {
            // Cache restoration is the last line of defense against losing a
            // valid graph. An allocation failure here cannot be recovered
            // without violating the cache ownership contract.
            std::terminate();
        }
    }

    [[nodiscard]] bool has_candidate() const noexcept {
        return candidate_.has_value();
    }

    [[nodiscard]] CompiledFrameGraph& candidate() {
        if (!candidate_) {
            throw std::logic_error("graph cache lease has no candidate");
        }
        return *candidate_;
    }

    /// Restore the checked-out graph to its original cache slot.
    void restore() {
        if (!candidate_) return;
        cache_.store(std::move(*candidate_), width_, height_);
        candidate_.reset();
    }

    /// Transfer the checked-out graph to the caller. The cache slot remains
    /// empty until the completed frame is published by frame_state_commit.
    [[nodiscard]] CompiledFrameGraph release() {
        if (!candidate_) {
            throw std::logic_error("graph cache lease has no candidate");
        }
        auto result = std::move(*candidate_);
        candidate_.reset();
        return result;
    }

private:
    CompiledGraphCache& cache_;
    std::optional<CompiledFrameGraph> candidate_;
    int width_;
    int height_;
};

} // namespace

[[nodiscard]] static inline uint64_t to_ms_u64(double ms) {
    return static_cast<uint64_t>(std::llround(std::max(0.0, ms)));
}

// The current graph model does not expose a universal processor-id field.
// Keep this diagnostic honest: report the canonical shape/node label rather
// than inventing an identity that the runtime cannot provide.
[[nodiscard]] static std::string diagnostic_processor_label(
    const RenderGraphNode& node) {
    if (const auto* source = dynamic_cast<const SourceNode*>(&node)) {
        return "shape:" + enum_utils::enum_name_lower_snake(source->render_node().shape.type());
    }
    if (const auto* multi = dynamic_cast<const MultiSourceNode*>(&node)) {
        if (!multi->items().empty() && multi->items().front().node) {
            return "shape:" + enum_utils::enum_name_lower_snake(
                multi->items().front().node->shape.type());
        }
        return "shape:none";
    }
    return "node:" + std::string(to_string(node.kind()));
}

static void log_graph_cache_diagnostics(
    const CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    std::string_view decision,
    std::string_view graph_cache_key)
{
    if (!ctx.policy.diagnostics_enabled) return;

    spdlog::info(
        "[graph-cache-diagnostic] frame={} graph_cache_scope={} graph_instance_id={} "
        "structure_hash={} graph_reused={} decision={} generation=unavailable "
        "graph_instance_id={} nodes={}",
        static_cast<int>(ctx.frame_input.frame), graph_cache_key,
        compiled.graph_instance_id.value, compiled.structure_hash,
        decision == "refresh_cached" ? 1 : 0, decision,
        compiled.graph_instance_id.value, compiled.graph.live_count());

    for (size_t id = 0; id < compiled.graph.size(); ++id) {
        if (!compiled.graph.has_node(static_cast<GraphNodeId>(id)) ||
            id >= compiled.nodes.size() || !compiled.nodes[id].reachable) {
            continue;
        }
        const auto& node = compiled.graph.node(static_cast<GraphNodeId>(id));
        const auto key = node.cache_key(ctx);
        const auto& info = compiled.nodes[id];
        spdlog::info(
            "[graph-node-diagnostic] frame={} node_cache_key_digest={} "
            "graph_instance_id={} node_id={} stable_node_id={} node_kind={} "
            "shape_label={} processor_id=unavailable processor_label={} "
            "refresh_decision={} inputs={}",
            static_cast<int>(ctx.frame_input.frame), key.digest(),
            compiled.graph_instance_id.value, id, info.stable_node_id.value,
            to_string(info.kind), diagnostic_processor_label(node),
            diagnostic_processor_label(node), decision, info.inputs.size());
    }
}

/// Shared built-in pipeline catalogs.  Initialized once and reused across
/// graph builds.  Populates graph_nodes + effects + layer transitions.
[[nodiscard]] static const PipelineCatalogs& builtin_pipeline_catalogs() {
    static const PipelineCatalogs s_catalogs = []() {
        PipelineCatalogs c;
        init_graph_pipeline_catalogs(c);
        return c;
    }();
    return s_catalogs;
}

/// Build a full render graph from scratch (when cached graph not available).
[[nodiscard]] static CompiledFrameGraph build_fresh_graph(
    RenderGraphContext& ctx,
    const Scene& scene,
    const detail::LayerResolutionResult& resolved)
{
    CHRONON_ZONE_C("build_graph", trace_category::kGraph);

    // Full build path via GraphBuildPipeline.
    // Uses build_with_resolved() to avoid redundant resolve_layers() call.
    auto mutable_ctx = ctx;
    GraphBuildPipeline pipeline;
    pipeline.add_default_passes();

    GraphBuildContext::ResolvedData pre_resolved;
    pre_resolved.layers = resolved.layers;
    pre_resolved.camera = resolved.camera;

    // The caller is responsible for wiring catalog pointers into the
    // original context before build_or_reuse_graph branches.  We still
    // wire the mutable copy used by the builder so node factories see
    // the same service pointers during graph construction.
    wire_catalog_pointers(mutable_ctx, builtin_pipeline_catalogs());

    RenderGraph graph = pipeline.build_with_resolved(scene, mutable_ctx, pre_resolved);

    // Carry forward context state set by the pipeline
    ctx.policy.skip_initial_clear = mutable_ctx.policy.skip_initial_clear;
    ctx.policy.track_dof_depth = mutable_ctx.policy.track_dof_depth;
    ctx.node_exec.dof_depth = std::move(mutable_ctx.node_exec.dof_depth);
    ctx.node_exec.dof_source_coverage = mutable_ctx.node_exec.dof_source_coverage;
    ctx.node_exec.early_exit_skip = std::move(mutable_ctx.node_exec.early_exit_skip);

    // Compile + optimize
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions compile_options;
    compile_options.run_optimizer = true;
    compile_options.compute_lifetimes = true;
    compile_options.compute_bboxes = true;
    compile_options.include_diagnostics = ctx.policy.diagnostics_enabled;

    auto compiled = compiler.compile(std::move(graph), ctx, compile_options);
    compiled.authored_structure_fingerprint =
        SceneHasher{}.compute_structure_fingerprint(
            scene, ctx.services.registry_generation);
    if (!compiled.valid) {
        throw std::runtime_error(
            "graph cache coordinator: fresh graph compilation produced an invalid graph");
    }
    return compiled;
}

/// Reuse the cached compiled graph, refreshing all node payloads
/// (source content, transforms, effect parameters) to match the
/// current frame data.
[[nodiscard]] static CompiledFrameGraph reuse_cached_graph(
    CompiledGraphCache& graph_cache,
    const Scene& scene,
    RenderGraphContext& ctx,
    const detail::LayerResolutionResult& resolved,
    int width,
    int height,
    std::uint64_t current_authored_topology,
    bool diagnostics_enabled,
    bool& graph_reused)
{
    graph_reused = false;
    CachedGraphLease lease{
        graph_cache,
        graph_cache.try_take(width, height),
        width,
        height};

    // A missing, invalid, or structurally stale candidate is a miss. Keep a
    // checked-out candidate under the lease while rebuilding: if compilation
    // or processor resolution throws, the lease restores the old graph.
    if (!lease.has_candidate()) {
        return build_fresh_graph(ctx, scene, resolved);
    }

    CompiledFrameGraph& compiled = lease.candidate();
    const auto current_snapshot = ctx.services.backend
        ? ctx.services.backend->processor_snapshot()
        : nullptr;
    const auto current_snapshot_identity = current_snapshot
        ? current_snapshot->identity()
        : 0;
    if (!compiled.valid ||
        compiled.registry_generation != ctx.services.registry_generation ||
        compiled.processor_snapshot_identity != current_snapshot_identity ||
        compiled.authored_structure_fingerprint == 0 ||
        compiled.authored_structure_fingerprint != current_authored_topology) {
        lease.restore();
        return build_fresh_graph(ctx, scene, resolved);
    }

    // DOF depth is execution state, not compiled-graph payload.  A reused
    // graph must receive a fresh sentinel-filled buffer for every frame so
    // CompositeNode can repopulate it before PerPixelDofNode executes.
    if (ctx.frame_input.has_camera_2_5d &&
        ctx.frame_input.camera_2_5d.dof.enabled) {
        ctx.policy.track_dof_depth = true;
        ctx.node_exec.dof_depth.assign(
            static_cast<size_t>(width) * static_cast<size_t>(height),
            kUnsetDofDepth);
        ctx.node_exec.dof_source_coverage.reset();
    } else {
        ctx.policy.track_dof_depth = false;
        ctx.node_exec.dof_depth.clear();
        ctx.node_exec.dof_source_coverage.reset();
    }

    compiled.graph.unfreeze_for_refresh();
    const auto t_refresh0 = profiling::now();
    detail::SceneRefreshResult refresh_result;
    try {
        refresh_result = detail::refresh_compiled_graph_payloads(
            compiled, scene, ctx, resolved);
    } catch (const std::exception& error) {
        // `try_take()` is a checkout, not an invalidation. The refresh
        // implementation prepares detached node patches and leaves this
        // candidate structurally intact when preparation throws, so restore
        // it before attempting the fallback build. If compilation fails,
        // the previous cache entry is still available for the next frame.
        if (diagnostics_enabled) {
            spdlog::warn("[graph-cache] transactional refresh threw: {} — restoring prior cache and rebuilding fresh",
                         error.what());
        }
        lease.restore();
        return build_fresh_graph(ctx, scene, resolved);
    } catch (...) {
        // Preserve the same cache invariant for non-standard exceptions.
        lease.restore();
        return build_fresh_graph(ctx, scene, resolved);
    }
    const auto t_refresh1 = profiling::now();

    if (!refresh_result) {
        // `compiled` was detached from the cache before refresh. Never return
        // a candidate that failed structural validation: rebuild a complete
        // graph instead, leaving the old cache unpublished and avoiding any
        // partially refreshed graph becoming the next cache entry.
        if (diagnostics_enabled) {
            spdlog::warn("[graph-cache] transactional refresh rejected: {} — rebuilding fresh",
                         refresh_result.message);
        }
        // Validation is performed before any refresh mutation, so this
        // detached candidate is still the original cache entry. Restore it
        // before compiling the fallback graph; the caller replaces it only
        // after the fresh graph has completed and the frame commits.
        lease.restore();
        return build_fresh_graph(ctx, scene, resolved);
    }

    graph_reused = true;

    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->compiled_graph_refresh_wall_ms.fetch_add(
            to_ms_u64(profiling::duration_ms(t_refresh0, t_refresh1)),
            std::memory_order_relaxed);
    }

    // Preserve optimization metadata computed during compilation. These
    // fields are part of the cached graph contract; resetting them on a warm
    // path can change clear/early-exit behavior and make sequential output
    // diverge from an independent cold render.
    ctx.policy.skip_initial_clear = compiled.skip_initial_clear;
    ctx.node_exec.early_exit_skip = compiled.early_exit_skip;

    if (diagnostics_enabled) {
        spdlog::info("[graph-cache] reusing cached compiled graph ({} live nodes)",
            compiled.graph.live_count());
    }

    log_graph_cache_diagnostics(
        compiled, ctx, "refresh_cached",
        "graph:" + std::to_string(width) + "x" + std::to_string(height));
    return lease.release();
}

GraphBuildResult build_or_reuse_graph(
    RenderGraphContext& ctx,
    const Scene& scene,
    const detail::LayerResolutionResult& resolved,
    int width,
    int height,
    bool scene_structure_unchanged,
    bool diagnostics_enabled)
{
    auto* graph_cache = ctx.services.compiled_graph_cache;

    GraphBuildResult result;

    // Wire catalog pointers (graph nodes, effects, layer transitions,
    // typed precomp builder, etc.) into the *real* context before any
    // branch is taken.  Previously this only happened inside
    // build_fresh_graph on a local copy of the context, so cached graphs
    // and the original context used by the executor saw a null catalog
    // pointer (e.g. LayerTransitionCatalog).
    wire_catalog_pointers(ctx, builtin_pipeline_catalogs());

    const auto current_authored_topology =
        SceneHasher{}.compute_structure_fingerprint(
            scene, ctx.services.registry_generation);
    result.can_reuse = scene_structure_unchanged &&
        graph_cache != nullptr &&
        graph_cache->has(width, height);

    const auto t_graph0 = profiling::now();

    if (result.can_reuse) {
        result.compiled = reuse_cached_graph(
            *graph_cache, scene, ctx, resolved, width, height,
            current_authored_topology, diagnostics_enabled, result.graph_reused);
        result.skip_initial_clear = result.compiled.skip_initial_clear;
    } else {
        // A fresh build is prepared outside the cache slot. If compilation or
        // processor resolution throws, the matching old entry is untouched.
        // Invalidate that entry only after the fresh candidate is complete;
        // frame_state_commit publishes the completed candidate atomically
        // after execution succeeds.
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->graph_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        // Keep any prior entry installed until frame_state_commit publishes
        // this completed candidate. This preserves the old cache if execution
        // or any later publication step fails after compilation succeeds.
        result.compiled = build_fresh_graph(ctx, scene, resolved);
        result.graph_reused = false;
        result.skip_initial_clear = ctx.policy.skip_initial_clear;
        log_graph_cache_diagnostics(
            result.compiled, ctx, "build_fresh",
            "graph:" + std::to_string(width) + "x" + std::to_string(height));
    }

    // Count only the final decision on the checked-out candidate. A
    // topology-rejected candidate is a rebuild/miss, not a cache hit.
    if (ctx.node_exec.counters && result.can_reuse) {
        if (result.graph_reused) {
            ctx.node_exec.counters->graph_cache_hits.fetch_add(1, std::memory_order_relaxed);
        } else {
            ctx.node_exec.counters->graph_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const auto t_graph1 = profiling::now();

    if (ctx.node_exec.counters && !result.graph_reused) {
        ctx.node_exec.counters->graph_build_wall_ms.fetch_add(
            to_ms_u64(profiling::duration_ms(t_graph0, t_graph1)),
            std::memory_order_relaxed);
    }

    return result;
}

} // namespace chronon3d::graph
