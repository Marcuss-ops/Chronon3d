#include "graph_cache_coordinator.hpp"

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>
#include <chronon3d/render_graph/pipeline/register_pipeline_nodes.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/trace_categories.hpp>
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include "../builder/graph_builder_internal.hpp"
#include "../builder/graph_builder_pipeline.hpp"
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

[[nodiscard]] static inline uint64_t to_ms_u64(double ms) {
    return static_cast<uint64_t>(std::llround(std::max(0.0, ms)));
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

    // Register pipeline graph node factories before building.
    // This must happen in graph_pipeline, not graph_builder, to avoid
    // reintroducing the CMake dependency cycle.
    register_pipeline_graph_nodes();

    // Wire the precomp build factory so PrecompNode (in graph_nodes)
    // can build + compile nested scenes without linking graph_builder.
    wire_precomp_build_factory(mutable_ctx);

    RenderGraph graph = pipeline.build_with_resolved(scene, mutable_ctx, pre_resolved);

    // Carry forward context state set by the pipeline
    ctx.options.skip_initial_clear = mutable_ctx.options.skip_initial_clear;
    ctx.tile.early_exit_skip = std::move(mutable_ctx.tile.early_exit_skip);

    // Compile + optimize
    FrameGraphCompiler compiler;
    FrameGraphCompileOptions compile_options;
    compile_options.run_optimizer = true;
    compile_options.compute_lifetimes = true;
    compile_options.compute_bboxes = true;
    compile_options.include_diagnostics = ctx.options.diagnostics_enabled;

    return compiler.compile(std::move(graph), ctx, compile_options);
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
    bool diagnostics_enabled)
{
    auto maybe_compiled = graph_cache.try_take(width, height);
    CompiledFrameGraph compiled;
    if (maybe_compiled) {
        compiled = std::move(*maybe_compiled);
    }

    const auto t_refresh0 = profiling::now();
    detail::refresh_compiled_graph_payloads(compiled, scene, ctx, resolved);
    const auto t_refresh1 = profiling::now();

    if (ctx.telemetry.counters) {
        ctx.telemetry.counters->compiled_graph_refresh_ms.fetch_add(
            to_ms_u64(profiling::duration_ms(t_refresh0, t_refresh1)),
            std::memory_order_relaxed);
    }

    compiled.skip_initial_clear = false;
    compiled.early_exit_skip.assign(compiled.graph.size(), false);

    ctx.options.skip_initial_clear = compiled.skip_initial_clear;
    ctx.tile.early_exit_skip = compiled.early_exit_skip;

    if (diagnostics_enabled) {
        spdlog::info("[graph-cache] reusing cached compiled graph ({} live nodes)",
            compiled.graph.live_count());
    }

    return compiled;
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
    auto* graph_cache = ctx.resources.compiled_graph_cache;

    GraphBuildResult result;
    result.can_reuse = scene_structure_unchanged &&
        graph_cache != nullptr &&
        graph_cache->has(width, height);

    const auto t_graph0 = profiling::now();

    if (result.can_reuse) {
        if (ctx.telemetry.counters) {
            ctx.telemetry.counters->graph_cache_hits.fetch_add(1, std::memory_order_relaxed);
        }
        result.compiled = reuse_cached_graph(
            *graph_cache, scene, ctx, resolved, width, height, diagnostics_enabled);
        result.graph_reused = true;
        result.skip_initial_clear = result.compiled.skip_initial_clear;
    } else {
        if (ctx.telemetry.counters) {
            ctx.telemetry.counters->graph_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        result.compiled = build_fresh_graph(ctx, scene, resolved);
        result.graph_reused = false;
        result.skip_initial_clear = ctx.options.skip_initial_clear;
    }

    const auto t_graph1 = profiling::now();

    if (ctx.telemetry.counters && !result.graph_reused) {
        ctx.telemetry.counters->graph_build_ms.fetch_add(
            to_ms_u64(profiling::duration_ms(t_graph0, t_graph1)),
            std::memory_order_relaxed);
    }

    return result;
}

} // namespace chronon3d::graph
