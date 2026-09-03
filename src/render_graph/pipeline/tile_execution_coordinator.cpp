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
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chronon3d::graph {

TileExecutionResult execute_tile_or_fallback(
    RenderGraphContext& ctx,
    CompiledFrameGraph& compiled,
    const detail::LayerResolutionResult& resolved,
    const RenderSettings& settings,
    const detail::DirtyRectOutput& dirty_out,
    const FrameExecutionPlan& execution_plan,
    double dirty_ratio,
    SoftwareRenderer* sw_renderer,
    Frame frame,
    int width,
    int height,
    ExecutionScope& root_scope)
{
    TileExecutionResult result;

    (void)resolved;
    // ExecutionResolver is the sole tile-prune authority. The coordinator
    // consumes only the immutable path and never checks dirty policy, ratio,
    // effects, or tile eligibility itself.
    result.use_tile_execution =
        execution_plan.path == FrameExecutionPath::SparseTiles;

    if (ctx.policy.diagnostics_enabled && !result.use_tile_execution) {
        spdlog::info(
            "[tile-debug] frame={} tile_execution_skipped dirty_ratio={:.3f} threshold={} reason={}",
            static_cast<int>(frame), dirty_ratio,
            settings.dirty.tile_dirty_ratio_threshold, execution_plan.reason);
    }

    if (result.use_tile_execution) {
        // ── Allocate final framebuffer ──────────────────────────────────
        {
            CHRONON_TRACE_SCOPE("chronon.frame", "tile_acquire");
            const auto* previous = execution_plan.copy_previous_surface
                ? (execution_plan.previous_framebuffer
                       ? execution_plan.previous_framebuffer.get()
                       : (execution_plan.previous_surface
                              ? execution_plan.previous_surface->cpu_framebuffer()
                              : nullptr))
                : nullptr;
            const bool have_prev = previous &&
                previous->width() == width && previous->height() == height;
            if (have_prev) {
                // Keep the pool-backed destination and copy directly row by
                // row. The resolver has already selected and described the
                // preserved surface in the immutable execution plan.
                result.fb = ctx.acquire_framebuffer(width, height, false);
                for (int y = 0; y < height; ++y) {
                    std::copy(previous->pixels_row(y),
                              previous->pixels_row(y) + width,
                              result.fb->pixels_row(y));
                }
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
            CHRONON_TRACE_SCOPE("chronon.graph", "tile_execute");
            const auto tile_start = profiling::now();
            const int total_tiles = dirty_out.tile_grid
                ? dirty_out.tile_grid->tile_count() : 0;
            const auto tile_result = detail::execute_dirty_tiles(
                compiled, ctx, sw_renderer, execution_plan, *result.fb,
                width, height, settings.dirty.parallel_tiles, root_scope);
            const int dirty_count = tile_result.dirty_count;
            const int clean_count = std::max(0, total_tiles - dirty_count);
            const uint64_t total_pixels = static_cast<uint64_t>(width) * height;
            const uint64_t pixels_rendered = std::min(
                total_pixels, tile_result.pixels_rendered);
            const auto tile_elapsed_ms = static_cast<uint64_t>(std::llround(
                std::max(0.0, profiling::duration_ms(tile_start, profiling::now()))));

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
                // A full-frame dirty mask still has a concrete tile
                // classification. Report its tile activity even when the
                // coalesced regions cover the complete canvas.
                if (dirty_count > 0 && total_tiles > 0 && clean_count == 0) {
                    ctx.node_exec.counters->tile_clean_count.fetch_add(
                        1, std::memory_order_relaxed);
                }
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
        // Full-frame clear/dirty handling was already resolved into the plan
        // before this coordinator was called. This branch only executes the
        // selected FullRgb contract; it does not inspect reasons or choose a
        // fallback policy.

        // ── Traditional single-pass execution ───────────────────────────
        {
            CHRONON_TRACE_SCOPE("chronon.graph", "graph_execute");
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
                result.fb = compatibility_executor.execute(
                    compiled, ctx, root_scope, compatibility_scheduler).framebuffer;
            } else {
                // PR 6.2 — root_scope constructed in
                // render_scene_via_graph() binds session + graph identity;
                // production calls use the sole runtime-owned executor.
                result.fb = sw_renderer->runtime().executor().execute(
                    compiled, ctx, root_scope,
                    sw_renderer->scheduler()).framebuffer;
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
        // A normal FullRgb decision (first frame, full-frame damage, spatial
        // effects, or missing previous surface) is not a tile fallback. The
        // counter is reserved for an execution failure after the resolver has
        // explicitly selected SparseTiles.
        if (execution_plan.path == FrameExecutionPath::SparseTiles &&
            ctx.node_exec.counters) {
            ctx.node_exec.counters->tile_full_fallbacks.fetch_add(
                1, std::memory_order_relaxed);
        }
    }

    return result;
}

} // namespace chronon3d::graph
