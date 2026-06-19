#include "tile_execution_coordinator.hpp"

#include "tile_execution_policy.hpp"
#include "scene_tile_execution.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/trace_categories.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/core/memory/render_session.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

TileExecutionResult execute_tile_or_fallback(
    RenderGraphContext& ctx,
    CompiledFrameGraph& compiled,
    const detail::LayerResolutionResult& resolved,
    const RenderSettings& settings,
    const detail::DirtyRectOutput& dirty_out,
    double dirty_ratio,
    SoftwareRenderer* sw_renderer,
    Frame frame,
    int width,
    int height)
{
    TileExecutionResult result;

    const TileDecision tile_decision = TileExecutionPolicy::decide(
        resolved, settings, dirty_out, dirty_ratio, sw_renderer, frame,
        ctx.resources.effect_catalog);
    result.use_tile_execution = tile_decision.enabled;

    if (ctx.options.diagnostics_enabled && !result.use_tile_execution) {
        spdlog::info(
            "[tile-debug] frame={} tile_execution_skipped dirty_ratio={:.3f} threshold={} reason={}",
            static_cast<int>(frame), dirty_ratio,
            settings.dirty.tile_dirty_ratio_threshold, tile_decision.reason_if_disabled);
    }

    if (result.use_tile_execution) {
        // ── Allocate final framebuffer ──────────────────────────────────
        {
            CHRONON_ZONE_C("tile_acquire", trace_category::kFrame);
            const bool have_prev = sw_renderer &&
                sw_renderer->buffer_ring().prev_framebuffer() &&
                sw_renderer->buffer_ring().prev_framebuffer()->width() == width &&
                sw_renderer->buffer_ring().prev_framebuffer()->height() == height;
            if (have_prev) {
                result.fb = ctx.acquire_framebuffer(
                    *sw_renderer->buffer_ring().prev_framebuffer());
            } else {
                result.fb = ctx.acquire_framebuffer(width, height, true);
            }
        }

        // ── Tile execution ──────────────────────────────────────────────
        {
            CHRONON_ZONE_C("tile_execute", trace_category::kGraph);
            const int total_tiles = dirty_out.tile_grid
                ? dirty_out.tile_grid->tile_count() : 0;
            const bool parallel_tiles = settings.dirty.parallel_tiles;

            auto tile_result = detail::execute_dirty_tiles(
                compiled, ctx, sw_renderer, dirty_out,
                *result.fb, width, height, parallel_tiles);

            // ── Tile counters ───────────────────────────────────────────
            if (ctx.telemetry.counters) {
                ctx.telemetry.counters->tile_dirty_count.fetch_add(
                    tile_result.dirty_count, std::memory_order_relaxed);
                const int clean_count = std::max(0, total_tiles - tile_result.dirty_count);
                ctx.telemetry.counters->tile_clean_count.fetch_add(
                    clean_count, std::memory_order_relaxed);
                ctx.telemetry.counters->tile_pixels_rendered.fetch_add(
                    tile_result.pixels_rendered, std::memory_order_relaxed);
                const uint64_t total_pixels =
                    static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
                const uint64_t pixels_skipped = (total_pixels > tile_result.pixels_rendered)
                    ? total_pixels - tile_result.pixels_rendered : 0;
                ctx.telemetry.counters->tile_pixels_skipped.fetch_add(
                    pixels_skipped, std::memory_order_relaxed);
            }

            if (ctx.options.diagnostics_enabled) {
                spdlog::info("[tile-debug] frame={} tile_total={} tile_dirty={}",
                    static_cast<int>(frame), total_tiles, tile_result.dirty_count);
            }
        }
    } else {
        // ── Traditional single-pass execution ───────────────────────────
        {
            CHRONON_ZONE_C("graph_execute", trace_category::kGraph);
            if (sw_renderer && sw_renderer->executor()) {
                result.fb = sw_renderer->executor()->execute(
                    compiled, ctx, sw_renderer->session());
            } else {
                RenderSession local_session;
                GraphExecutor local_executor;
                result.fb = local_executor.execute(compiled, ctx, local_session);
            }
        }
        // Track tile fallbacks when tile system requested but couldn't execute
        if (dirty_out.use_dirty_tiles && ctx.telemetry.counters) {
            ctx.telemetry.counters->tile_full_fallbacks.fetch_add(
                1, std::memory_order_relaxed);
        }
    }

    return result;
}

} // namespace chronon3d::graph
