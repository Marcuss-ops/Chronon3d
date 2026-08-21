#include "tile_execution_coordinator.hpp"

#include "tile_execution_policy.hpp"
#include "scene_tile_execution.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/tracing/tracing_categories.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
// TICKET-038/TXT-00 — canonical include for sw_renderer->runtime().
// RenderRuntime (in <chronon3d/runtime/render_runtime.hpp>) is the SOLE
// owner of GraphExecutor per TICKET-011.  Calling
// sw_renderer->runtime().executor().execute_with_scope(...) below at the
// fallback path needs the FULL type of RenderRuntime (the .executor()
// accessor + the GraphExecutor& return — only declared in the full
// type).  `render_session.hpp` alone forward-declares RenderRuntime via
// transitive include; the explicit include here resolves the type for
// this TU.  See commit 91debc36 (TXT-00 closure of ROT 1 on
// src/render_graph/pipeline/) for the duplicate-fix precedent — the
// audit-comment block above mirrors the wording exactly and the sister
// .cpp file `scene_tile_execution.cpp` carries the symmetric fix.
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>     // PR 6.4 — typed scope plumbing
#include <chronon3d/core/memory/arena.hpp>              // PR 6.4 — explicit child FrameArena
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>
#include <vector>

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
    int height,
    ExecutionScope& root_scope)
{
    TileExecutionResult result;

    const TileDecision tile_decision = TileExecutionPolicy::decide(
        resolved, settings, dirty_out, dirty_ratio, sw_renderer, frame,
        ctx.services.effect_catalog);
    result.use_tile_execution = tile_decision.enabled;

    if (ctx.policy.diagnostics_enabled && !result.use_tile_execution) {
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
                const auto previous = sw_renderer->buffer_ring().prev_framebuffer();
                std::vector<Color> previous_pixels(
                    previous->data(),
                    previous->data() + static_cast<size_t>(width) * static_cast<size_t>(height));
                // Do not use the reusable-bottom swap path here: tile
                // rendering needs the previous frame to remain available as
                // the restore source for every dirty region.
                result.fb = ctx.acquire_framebuffer(width, height, false);
                std::copy(previous_pixels.begin(), previous_pixels.end(), result.fb->data());
            } else {
                result.fb = ctx.acquire_framebuffer(width, height, true);
            }
        }

        // ── Tile execution ──────────────────────────────────────────────
        // The tile path must execute only the coalesced dirty regions.  A
        // previous implementation selected this branch but ran the complete
        // graph with an unclipped context, making the tile counters merely an
        // estimate and paying full-frame work while reporting pixels skipped.
        {
            CHRONON_ZONE_C("tile_execute", trace_category::kGraph);
            const auto tile_start = profiling::now();
            const int total_tiles = dirty_out.tile_grid
                ? dirty_out.tile_grid->tile_count() : 0;
            const auto tile_result = detail::execute_dirty_tiles(
                compiled, ctx, sw_renderer, dirty_out, *result.fb,
                width, height, settings.dirty.parallel_tiles, root_scope);
            const int dirty_count = tile_result.dirty_count;
            const int clean_count = std::max(0, total_tiles - dirty_count);
            const uint64_t total_pixels = static_cast<uint64_t>(width) * height;
            const uint64_t pixels_rendered = std::min(
                total_pixels, tile_result.pixels_rendered);
            const auto tile_elapsed_ms = static_cast<uint64_t>(std::llround(
                std::max(0.0, profiling::duration_ms(tile_start, profiling::now()))));

            if (ctx.node_exec.counters && dirty_out.dirty_rect && !dirty_out.dirty_rect->is_empty()) {
                const auto& dirty = *dirty_out.dirty_rect;
                const auto area = static_cast<uint64_t>(std::max(0, dirty.x1 - dirty.x0)) *
                    static_cast<uint64_t>(std::max(0, dirty.y1 - dirty.y0));
                ctx.node_exec.counters->dirty_rect_count.fetch_add(1, std::memory_order_relaxed);
                ctx.node_exec.counters->dirty_pixels.fetch_add(area, std::memory_order_relaxed);
            }

            // ── Tile counters ───────────────────────────────────────────
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->tile_dirty_count.fetch_add(
                    dirty_count, std::memory_order_relaxed);
                ctx.node_exec.counters->tile_clean_count.fetch_add(
                    clean_count, std::memory_order_relaxed);
                ctx.node_exec.counters->tile_pixels_rendered.fetch_add(
                    pixels_rendered, std::memory_order_relaxed);
                const uint64_t pixels_skipped = (total_pixels > pixels_rendered)
                    ? total_pixels - pixels_rendered : 0;
                ctx.node_exec.counters->tile_pixels_skipped.fetch_add(
                    pixels_skipped, std::memory_order_relaxed);
                ctx.node_exec.counters->tile_regions_executed.fetch_add(
                    tile_result.regions_executed, std::memory_order_relaxed);
                ctx.node_exec.counters->tile_region_pixels.fetch_add(
                    pixels_rendered, std::memory_order_relaxed);
                ctx.node_exec.counters->tile_execution_wall_ms.fetch_add(
                    tile_elapsed_ms, std::memory_order_relaxed);
            }

            if (ctx.policy.diagnostics_enabled) {
                spdlog::info("[tile-debug] frame={} tile_total={} tile_dirty={} regions={} pixels_rendered={} elapsed_ms={}",
                    static_cast<int>(frame), total_tiles, dirty_count,
                    tile_result.regions_executed, pixels_rendered, tile_elapsed_ms);
            }
        }
    } else {
        // A cost-model rejection is a real execution-mode switch, not a
        // dirty-region fallback.  Keeping dirty clipping enabled here would
        // make a non-tiled execution render only the changed regions into a
        // fresh framebuffer (especially when ping-pong is unavailable),
        // leaving the untouched canvas transparent.  A rejected tile plan
        // therefore has to use the ordinary full-frame contract.
        if (tile_decision.reason_if_disabled == "cost_model_tile_slower") {
            ctx.policy.dirty_rects_enabled = false;
            ctx.policy.reuse_prev_framebuffer = false;
            ctx.policy.skip_initial_clear = false;
            ctx.node_exec.clip_rect.reset();
            ctx.node_exec.dirty_rect.reset();
        }

        // ── Traditional single-pass execution ───────────────────────────
        {
            CHRONON_ZONE_C("graph_execute", trace_category::kGraph);
            // Section 5 violation fix: executor is engine-owned by RenderRuntime,
            // not by SoftwareRenderer.  Reach it via runtime().executor().
            if (!sw_renderer || !sw_renderer->has_runtime()) {
                // Direct graph-pipeline tests may provide a backend without
                // the SoftwareRenderer sidecar. Preserve that explicit
                // compatibility path; production renderer calls always use
                // the runtime-owned executor below.
                GraphExecutor compatibility_executor;
                ExecutionScheduler compatibility_scheduler{
                    SchedulerMode::Sequential, 1, false};
                result.fb = compatibility_executor.execute_with_scope(
                    compiled, ctx, root_scope, compatibility_scheduler);
            } else {
                // PR 6.2 — root_scope constructed in
                // render_scene_via_graph() binds session + graph identity;
                // production calls use the sole runtime-owned executor.
                result.fb = sw_renderer->runtime().executor().execute_with_scope(
                    compiled, ctx, root_scope,
                    sw_renderer->scheduler());
            }
            // P0-1 — GraphExecutor returns nullptr when a node surfaced a
            // backend error (frame_error slot).  Propagate the null result
            // so the caller can detect the failure.
            if (!result.fb) {
                spdlog::error(
                    "[tile-exec] frame {} single-pass execution failed "
                    "(executor returned null — check frame_error for details)",
                    static_cast<int>(frame));
            }
        }
        // Track tile fallbacks when tile system requested but couldn't execute
        if (dirty_out.use_dirty_tiles && ctx.node_exec.counters) {
            ctx.node_exec.counters->tile_full_fallbacks.fetch_add(
                1, std::memory_order_relaxed);
        }
    }

    return result;
}

} // namespace chronon3d::graph
