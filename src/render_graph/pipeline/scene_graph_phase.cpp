// ---------------------------------------------------------------------------
// scene_graph_phase.cpp — Phase 3 of the render-scene-via-graph pipeline
//
// Wire up ping-pong framebuffers and transform scratch, then build (or reuse)
// the compiled render graph.  Pre-frame pool preallocation runs here.
// ---------------------------------------------------------------------------

#include "scene_phases.hpp"
#include "scene_internal.hpp"
#include "scene_refresh.hpp"

#include "../builder/graph_builder_pipeline.hpp"
#include "../builder/graph_builder_internal.hpp"
#include "../builder/graph_builder_coordinates.hpp"
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/optimizer/graph_optimizer.hpp>
#include <chronon3d/render_graph/preflight/preflight_render_graph.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>

#include <chrono>
#include <cstdlib>
#include <string_view>
#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace chronon3d::graph::detail {

namespace {

/// Predict framebuffer bucket sizes the compiled graph will need based on
/// each node's predicted_bbox (or canvas size as fallback).
/// Returns a deduplicated list of preallocation options with counts that
/// reflect the max number of concurrent framebuffers needed per level.
std::vector<cache::FramebufferPoolPreallocOptions> predict_pool_requirements(
    const CompiledFrameGraph& compiled,
    int canvas_width,
    int canvas_height)
{
    if (compiled.empty() || compiled.levels.empty()) {
        return {};
    }

    // Map bucket key -> max concurrent count (per-level peak)
    std::unordered_map<cache::FramebufferPoolKey, size_t, cache::FramebufferPoolKeyHash> peak_counts;

    for (const auto& level : compiled.levels) {
        std::unordered_map<cache::FramebufferPoolKey, size_t, cache::FramebufferPoolKeyHash> level_counts;
        for (GraphNodeId id : level) {
            if (id >= compiled.nodes.size()) continue;
            const auto& info = compiled.nodes[id];
            if (!info.reachable) continue;

            int w = canvas_width;
            int h = canvas_height;
            if (info.predicted_bbox && !info.predicted_bbox->is_empty()) {
                w = std::max(0, info.predicted_bbox->x1 - info.predicted_bbox->x0);
                h = std::max(0, info.predicted_bbox->y1 - info.predicted_bbox->y0);
            }
            const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(w, h);
            if (bw > 0 && bh > 0) {
                ++level_counts[cache::FramebufferPoolKey{bw, bh}];
            }
        }
        for (const auto& [bucket, cnt] : level_counts) {
            auto& peak = peak_counts[bucket];
            peak = std::max(peak, cnt);
        }
    }

    // With the async video pipeline (render thread + queue + writer thread),
    // up to 4 buffers per bucket can be in flight simultaneously.
    // kMaxPreallocPerBucket must cover: 1 current + 1 previous + 1 queued + 1 encoding.
    constexpr size_t kMaxPreallocPerBucket = 5;
    std::vector<cache::FramebufferPoolPreallocOptions> predictions;
    predictions.reserve(peak_counts.size() + 1);

    // Always ensure the main canvas bucket has at least 4 warm buffers.
    {
        const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(canvas_width, canvas_height);
        predictions.push_back(cache::FramebufferPoolPreallocOptions{
            .width  = bw,
            .height = bh,
            .count  = 4,
            .clear  = true,
            .touch_memory = false,
        });
    }

    for (const auto& [bucket, count] : peak_counts) {
        const size_t capped_count = std::min(count, kMaxPreallocPerBucket);
        predictions.push_back(cache::FramebufferPoolPreallocOptions{
            .width  = bucket.width,
            .height = bucket.height,
            .count  = capped_count,
            .clear  = true,
            .touch_memory = false,
        });
    }
    return predictions;
}

} // anonymous namespace

void run_graph_build_phase(SceneRenderContext& sctx) {
    SoftwareRenderer* sw_renderer = sctx.sw_renderer;
    const i32 width  = sctx.width;
    const i32 height = sctx.height;
    const Frame frame = sctx.frame;
    const Scene& scene = *sctx.scene;
    const RenderSettings& settings = *sctx.settings;

    // ── Wire up ping-pong framebuffers AND transform scratch ──────────
    // Both must be set before graph build/reuse for ALL code paths.
    // Ping-pong: ClearNode uses the exclusive write ping instead of COW.
    // Disable with CHRONON_PINGPONG_FRAMEBUFFER=0 env var for benchmarking.
    // Transform scratch: TransformNode reuses a persistent buffer.
    static const bool s_pingpong_enabled = []() -> bool {
        const char* env = std::getenv("CHRONON_PINGPONG_FRAMEBUFFER");
        if (env) return std::string_view(env) != "0";
        return true; // enabled by default
    }();
    if (sw_renderer) {
        if (s_pingpong_enabled) {
            sw_renderer->ensure_ping_framebuffers(width, height);
            sctx.ctx.ping_write_fb    = sw_renderer->m_ping_fb[sw_renderer->m_ping_write_idx];
            sctx.ctx.ping_write_slot  = sw_renderer->write_ping_slot();
        }

        sctx.ctx.transform_scratch       = sw_renderer->ensure_transform_scratch(width, height);
        sctx.ctx.transform_scratch_slot   = sw_renderer->transform_scratch_slot();
    }

    // ── Build (or reuse cached) render graph ────────────────────────────
    const bool can_reuse_compiled_graph =
        sctx.ctx.graph_structure_unchanged &&
        sw_renderer &&
        sw_renderer->m_cached_compiled_graph != nullptr &&
        sw_renderer->m_cached_compiled_width  == width &&
        sw_renderer->m_cached_compiled_height == height;

    sctx.t_graph0 = std::chrono::steady_clock::now();
    sctx.graph_reused = false;

    if (can_reuse_compiled_graph) {
        // Reuse the cached compiled graph from the previous frame
        if (sctx.ctx.counters) {
            sctx.ctx.counters->graph_cache_hits.fetch_add(1, std::memory_order_relaxed);
        }
        sctx.compiled = std::move(*sw_renderer->m_cached_compiled_graph);
        sw_renderer->m_cached_compiled_graph.reset();

        const auto t_refresh0 = std::chrono::steady_clock::now();
        detail::refresh_compiled_graph_payloads(sctx.compiled, scene, sctx.ctx, sctx.resolved);
        const auto t_refresh1 = std::chrono::steady_clock::now();
        if (sctx.ctx.counters) {
            sctx.ctx.counters->compiled_graph_refresh_ms.fetch_add(
                to_ms_u64(std::chrono::duration<double, std::milli>(t_refresh1 - t_refresh0).count()),
                std::memory_order_relaxed);
        }
        sctx.compiled.skip_initial_clear = false;
        sctx.compiled.early_exit_skip.assign(sctx.compiled.graph.size(), false);

        sctx.ctx.skip_initial_clear = sctx.compiled.skip_initial_clear;
        sctx.ctx.early_exit_skip    = sctx.compiled.early_exit_skip;

        if (sctx.ctx.diagnostics_enabled) {
            spdlog::info("[graph-cache] frame={} reusing cached compiled graph ({} live nodes)",
                static_cast<int>(frame), sctx.compiled.graph.live_count());
        }
        sctx.graph_reused = true;
    } else {
        if (sctx.ctx.counters) {
            sctx.ctx.counters->graph_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        RenderGraph graph;
        // Full build path — construct the render graph via GraphBuildPipeline.
        // Uses build_with_resolved() to avoid a redundant resolve_layers() call
        // since we already resolved above for dirty-rect computation.
        {
            CHRONON_ZONE_C("build_graph", trace_category::kGraph);
            auto mutable_ctx = sctx.ctx;
            GraphBuildPipeline pipeline;
            pipeline.add_default_passes();
            GraphBuildContext::ResolvedData pre_resolved;
            pre_resolved.layers  = sctx.resolved.layers;
            pre_resolved.camera  = sctx.resolved.camera;
            graph = pipeline.build_with_resolved(scene, mutable_ctx, pre_resolved);
            sctx.ctx.skip_initial_clear = mutable_ctx.skip_initial_clear;
            sctx.ctx.early_exit_skip    = std::move(mutable_ctx.early_exit_skip);
        }

        if (sctx.ctx.diagnostics_enabled) {
            for (size_t i = 0; i < graph.size(); ++i) {
                if (!graph.has_node(i)) continue;
                std::string inputs_str;
                for (auto in : graph.inputs(i)) {
                    inputs_str += std::to_string(in) + " (" + graph.node(in).name() + "), ";
                }
                spdlog::info("[graph-structure] node_id={} name='{}' inputs=[{}]",
                    i, graph.node(i).name(), inputs_str);
            }
        }

        // Compile path (which includes optimization)
        FrameGraphCompiler compiler;
        FrameGraphCompileOptions compile_options;
        compile_options.run_optimizer      = true;
        compile_options.compute_lifetimes  = true;
        compile_options.compute_bboxes     = true;  // always needed for pool preallocation sizing
        compile_options.include_diagnostics = settings.diagnostic_plan;

        sctx.compiled = compiler.compile(std::move(graph), sctx.ctx, compile_options);
    }
    sctx.t_graph1 = std::chrono::steady_clock::now();

    // ── Pre-frame pool preallocation ────────────────────────────────────
    // Ensure the exact bucket sizes the upcoming frame will need are already
    // in the pool, eliminating allocation stalls during graph execution.
    if (sw_renderer && sw_renderer->framebuffer_pool()) {
        const auto t_prealloc0 = std::chrono::steady_clock::now();
        auto predictions = predict_pool_requirements(sctx.compiled, width, height);
        auto pool = sw_renderer->framebuffer_pool();
        size_t prealloc_total = 0;
        for (const auto& pred : predictions) {
            prealloc_total += pool->ensure_preallocated(pred);
        }
        const auto t_prealloc1 = std::chrono::steady_clock::now();
        const double prealloc_ms =
            std::chrono::duration<double, std::milli>(t_prealloc1 - t_prealloc0).count();
        if (sctx.ctx.counters) {
            sctx.ctx.counters->framebuffer_prealloc_created.fetch_add(
                prealloc_total, std::memory_order_relaxed);
        }
        if (sctx.ctx.diagnostics_enabled) {
            spdlog::info("[pool-prealloc] frame={} ensured={} predictions={} ms={:.2f}",
                static_cast<int>(frame), prealloc_total, predictions.size(), prealloc_ms);
        }
    }
}

} // namespace chronon3d::graph::detail
