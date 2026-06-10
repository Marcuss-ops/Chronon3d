// ---------------------------------------------------------------------------
// scene_execute_phase.cpp — Phase 4 of the render-scene-via-graph pipeline
//
// Tile-based execution (when enabled and safe) or traditional single-pass
// graph execution.  Also records timing counters and per-frame telemetry.
// ---------------------------------------------------------------------------

#include "scene_phases.hpp"
#include "scene_internal.hpp"

// graph_executor.hpp provides GraphExecutor + FrameArena; must be included
// BEFORE scene_tile_execution.hpp (which uses FrameArena in inline functions).
#include <chronon3d/render_graph/executor/graph_executor.hpp>

#include "scene_tile_execution.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <chrono>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::detail {

void run_execute_phase(SceneRenderContext& sctx) {
    SoftwareRenderer* sw_renderer = sctx.sw_renderer;
    const i32 width  = sctx.width;
    const i32 height = sctx.height;
    const Frame frame = sctx.frame;
    const RenderSettings& settings = *sctx.settings;

    // Tile execution is incompatible with spatial effects (glow, bloom, drop
    // shadow, blur, distort, temporal).  When tile execution re-executes the
    // full graph per tile, the effect stack runs with a tile-scoped clip_rect.
    // The blur kernel in these effects reads pixels outside the tile boundary
    // — those pixels are zero or garbage (from the fresh per-tile framebuffer),
    // producing visible seams at tile edges.  Disable tile execution when ANY
    // layer has spatial effects.
    const bool tile_safe = !detail::has_layer_with_spatial_effects(sctx.resolved, frame);

    // ── Dirty-ratio threshold for tile execution ────────────────────────
    // When the dirty area covers >threshold of the screen, per-tile graph
    // re-execution overhead dominates and single-pass is faster.
    const bool dirty_ratio_below_threshold =
        sctx.dirty_ratio <= settings.tile_dirty_ratio_threshold;

    sctx.use_tile_execution = tile_safe &&
                              sctx.dirty_out.use_dirty_tiles &&
                              dirty_ratio_below_threshold &&
                              sw_renderer &&
                              sw_renderer->executor() &&
                              sctx.dirty_out.tile_grid.has_value() &&
                              sctx.dirty_out.dirty_tiles.has_value() &&
                              sctx.dirty_out.dirty_tiles->any();

    if (sctx.ctx.diagnostics_enabled && !dirty_ratio_below_threshold) {
        spdlog::info(
            "[tile-debug] frame={} tile_execution_skipped dirty_ratio={:.3f} threshold={} reason=high_dirty_ratio",
            static_cast<int>(frame), sctx.dirty_ratio, settings.tile_dirty_ratio_threshold);
    }

    sctx.t_exec0 = std::chrono::steady_clock::now();

    if (sctx.use_tile_execution) {
        // ── Allocate final framebuffer: copy previous frame for clean tiles ──
        {
            CHRONON_ZONE_C("tile_acquire", trace_category::kFrame);
            const bool have_prev = sw_renderer->m_prev_framebuffer &&
                                   sw_renderer->m_prev_framebuffer->width()  == width &&
                                   sw_renderer->m_prev_framebuffer->height() == height;
            if (have_prev) {
                sctx.fb_shared = sctx.ctx.acquire_framebuffer(*sw_renderer->m_prev_framebuffer);
            } else {
                sctx.fb_shared = sctx.ctx.acquire_framebuffer(width, height, true);
            }
        }

        // ── Tile execution (parallel or sequential) ─────────────────────────
        {
            CHRONON_ZONE_C("tile_execute", trace_category::kGraph);
            const int total_tiles = sctx.dirty_out.tile_grid->tile_count();
            const bool parallel_tiles = sw_renderer->settings().enable_parallel_tiles;

            auto tile_result = detail::execute_dirty_tiles(
                sctx.compiled, sctx.ctx, sw_renderer, sctx.dirty_out,
                *sctx.fb_shared, width, height, parallel_tiles);

            // ── Tile counters ───────────────────────────────────────────────
            if (sctx.ctx.counters) {
                sctx.ctx.counters->tile_dirty_count.fetch_add(
                    tile_result.dirty_count, std::memory_order_relaxed);
                const int clean_count = std::max(0, total_tiles - tile_result.dirty_count);
                sctx.ctx.counters->tile_clean_count.fetch_add(
                    clean_count, std::memory_order_relaxed);
                sctx.ctx.counters->tile_pixels_rendered.fetch_add(
                    tile_result.pixels_rendered, std::memory_order_relaxed);
                const uint64_t total_pixels =
                    static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
                const uint64_t pixels_skipped = (total_pixels > tile_result.pixels_rendered)
                    ? total_pixels - tile_result.pixels_rendered : 0;
                sctx.ctx.counters->tile_pixels_skipped.fetch_add(
                    pixels_skipped, std::memory_order_relaxed);
            }

            if (sctx.ctx.diagnostics_enabled) {
                spdlog::info("[tile-debug] frame={} tile_total={} tile_dirty={}",
                    static_cast<int>(frame), total_tiles, tile_result.dirty_count);
            }
        }
    } else {
        // ── Traditional single-pass execution ───────────────────────────────
        {
            CHRONON_ZONE_C("graph_execute", trace_category::kGraph);
            if (sw_renderer && sw_renderer->executor()) {
                sctx.fb_shared = sw_renderer->executor()->execute(sctx.compiled, sctx.ctx);
            } else {
                GraphExecutor local_executor;
                sctx.fb_shared = local_executor.execute(sctx.compiled, sctx.ctx);
            }
        }
        // Track tile fallbacks when tile system requested but couldn't execute
        if (sctx.dirty_out.use_dirty_tiles && sctx.ctx.counters) {
            sctx.ctx.counters->tile_full_fallbacks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    sctx.t_exec1 = std::chrono::steady_clock::now();

    // ── Timing counters ──────────────────────────────────────────────────
    if (sctx.ctx.counters || sctx.ctx.diagnostics_enabled) {
        const double resolve_ms = std::chrono::duration<double, std::milli>(
            sctx.t_resolve1 - sctx.t_resolve0).count();
        const double dirty_ms = std::chrono::duration<double, std::milli>(
            sctx.t_dirty1 - sctx.t_dirty0).count();
        const double graph_ms = std::chrono::duration<double, std::milli>(
            sctx.t_graph1 - sctx.t_graph0).count();
        const double exec_ms = std::chrono::duration<double, std::milli>(
            sctx.t_exec1 - sctx.t_exec0).count();
        const double total_graph_ms = resolve_ms + dirty_ms + graph_ms + exec_ms;

        if (sctx.ctx.counters) {
            sctx.ctx.counters->graph_resolve_layers_ms.fetch_add(
                to_ms_u64(resolve_ms), std::memory_order_relaxed);
            sctx.ctx.counters->graph_dirty_rect_ms.fetch_add(
                to_ms_u64(dirty_ms), std::memory_order_relaxed);
            sctx.ctx.counters->graph_build_ms.fetch_add(
                to_ms_u64(graph_ms), std::memory_order_relaxed);
            sctx.ctx.counters->graph_execute_ms.fetch_add(
                to_ms_u64(exec_ms), std::memory_order_relaxed);
            sctx.ctx.counters->graph_total_ms.fetch_add(
                to_ms_u64(total_graph_ms), std::memory_order_relaxed);
        }

        if (sctx.ctx.diagnostics_enabled) {
            spdlog::info(
                "[graph-timing] frame={} resolve_layers_ms={:.2f} dirty_rect_ms={:.2f} "
                "graph_phase_ms={:.2f} graph_exec_ms={:.2f} graph_total_ms={:.2f} graph_reused={}",
                static_cast<int>(frame), resolve_ms, dirty_ms, graph_ms, exec_ms,
                total_graph_ms, sctx.graph_reused ? 1 : 0);
        }
    }

    // ── Record per-frame dirty-rect telemetry on the renderer ──────────
    if (sw_renderer) {
        sw_renderer->m_last_dirty_rect_enabled = sctx.dirty_out.use_dirty_rects;
        sw_renderer->m_last_dirty_rect         = sctx.dirty_out.dirty_rect;
        sw_renderer->m_last_tile_execution_used = sctx.use_tile_execution;
        sw_renderer->m_last_fast_path_reused   = false;
        sw_renderer->m_last_graph_reused       = sctx.graph_reused;
    }
}

} // namespace chronon3d::graph::detail
