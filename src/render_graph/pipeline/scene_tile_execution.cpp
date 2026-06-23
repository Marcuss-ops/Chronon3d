#include "scene_tile_execution.hpp"

#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>     // PR 6.4 — typed scope plumbing
#include <chronon3d/core/memory/arena.hpp>              // PR 6.4 — explicit child FrameArena
#include <algorithm>

namespace chronon3d::graph::detail {

// ── Tile coalescing ───────────────────────────────────────────────────────
// Merges adjacent dirty tiles into larger bounding boxes to reduce the
// number of graph re-executions.  A moving object that spans 2×3 tiles
// (6 re-executions) is coalesced into 1 execution — a 6× reduction in
// graph overhead (cache evaluation, context cloning, schedule overhead).
//
// Algorithm: greedy row-wise merge.  Each row's dirty tiles become a
// horizontal strip; vertically adjacent strips with the same x-range
// are merged into a single region.
//
// Note: rows with non-contiguous dirty tiles (e.g. cols 0-1 and 4-5 dirty
// but 2-3 clean) are merged into one wide region spanning cols 0-5.  This
// may render clean pixels in the gap, trading a larger region for fewer
// graph re-executions.  For typical contiguous-dirty workloads this is a
// net win.

[[nodiscard]] std::vector<raster::BBox> coalesce_dirty_tiles(
    const raster::TileGrid& grid,
    const raster::DirtyTileMask& mask)
{
    std::vector<raster::BBox> regions;
    const int cols = grid.cols();
    const int rows = grid.rows();

    for (int ty = 0; ty < rows; ++ty) {
        // Find the horizontal span of dirty tiles in this row.
        int tx_min = cols, tx_max = -1;
        for (int tx = 0; tx < cols; ++tx) {
            if (mask.is_dirty(tx, ty)) {
                tx_min = std::min(tx_min, tx);
                tx_max = std::max(tx_max, tx);
            }
        }
        if (tx_min > tx_max) continue;  // no dirty tiles in this row

        // Build the pixel-space bounding box for this row's dirty span.
        raster::BBox row_region = grid.tile_bounds(tx_min, ty);
        row_region.x1 = grid.tile_bounds(tx_max, ty).x1;

        // Try to merge vertically with the previous region.
        if (!regions.empty()) {
            raster::BBox& prev = regions.back();
            if (prev.y1 == row_region.y0 &&
                prev.x0 == row_region.x0 &&
                prev.x1 == row_region.x1) {
                prev.y1 = row_region.y1;  // extend vertically
                continue;
            }
        }
        regions.push_back(row_region);
    }

    return regions;
}

// ── Execute a single dirty region: set up clip context, run graph, copy to output.
[[nodiscard]] static TileExecutionResult execute_single_dirty_region(
    CompiledFrameGraph& compiled,
    const RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer,
    const raster::BBox& region_bbox,
    Framebuffer& output_fb,
    const ExecutionScope& root_scope
) {
    RenderGraphContext tile_ctx = ctx;
    tile_ctx.node_exec.clip_rect = region_bbox;
    tile_ctx.node_exec.dirty_rect = region_bbox;
    tile_ctx.policy.reuse_prev_framebuffer = false;
    tile_ctx.policy.tile_execution_enabled = true;
    tile_ctx.node_exec.active_tile_clip = region_bbox;
    tile_ctx.policy.skip_initial_clear = false;
    tile_ctx.node_exec.early_exit_skip.clear();

    // PR 6.2 — root_scope is constructed once in render_scene_via_graph()
    // and threaded through.  Each tile region creates a child Tile scope
    // with its own FrameArena (per-region isolation) but borrows the
    // root scope's session (shared across all tile regions).  The
    // parent chain is: root_scope → tile_scope, so child teardown
    // (ArenaGuard on tile_scope.arena()) never touches the root arena.
    //
    // The sw_renderer gate selects the executor/scheduler (renderer-owned
    // vs local), not the session — both paths use root_scope.session().
    std::shared_ptr<Framebuffer> tile_fb;
    FrameArena child_arena;   // PR 6.4 — distinct child arena per region
    ExecutionScope tile_scope(ExecutionScopeKind::Tile, root_scope.session(),
                              child_arena, compiled.graph_instance_id,
                              &root_scope);
    // Section 5 violation fix: executor lives on RenderRuntime (engine-
    // lifetime owner), not on SoftwareRenderer.  Reach it via
    // `sw_renderer->runtime().executor()`; the reference is guaranteed
    // non-null once HasRuntime() is true.
    if (!sw_renderer || !sw_renderer->has_runtime()) {
        GraphExecutor local_executor;
        ExecutionScheduler local_scheduler{SchedulerMode::Sequential, 1, false};
        tile_fb = local_executor.execute_with_scope(
            compiled, tile_ctx, tile_scope, local_scheduler);
    } else {
        auto& tile_scheduler = ctx.services.scheduler
            ? *ctx.services.scheduler
            : sw_renderer->scheduler();
        tile_fb = sw_renderer->runtime().executor().execute_with_scope(
            compiled, tile_ctx, tile_scope,
            tile_scheduler);
    }

    if (tile_fb) {
        for (i32 y = region_bbox.y0; y < region_bbox.y1; ++y) {
            std::copy(
                tile_fb->pixels_row(y) + region_bbox.x0,
                tile_fb->pixels_row(y) + region_bbox.x1,
                output_fb.pixels_row(y) + region_bbox.x0);
        }
    }

    return TileExecutionResult{
        .dirty_count = 1,
        .pixels_rendered =
            static_cast<uint64_t>(region_bbox.x1 - region_bbox.x0) *
            static_cast<uint64_t>(region_bbox.y1 - region_bbox.y0)
    };
}

TileExecutionResult execute_dirty_tiles(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer,
    const DirtyRectOutput& dirty_out,
    Framebuffer& output_fb,
    i32 width,
    i32 height,
    bool parallel,
    ExecutionScope& root_scope
) {
    const auto& tile_grid = *dirty_out.tile_grid;
    const auto& dirty_tiles = *dirty_out.dirty_tiles;

    // ── Coalesce adjacent dirty tiles into larger regions ──────────────
    // This reduces the number of full-graph re-executions.  For example,
    // 6 adjacent dirty tiles become 1 region → 6× less graph overhead.
    const auto regions = coalesce_dirty_tiles(tile_grid, dirty_tiles);

    // Count original dirty tiles for accurate telemetry.
    const int original_dirty = dirty_tiles.dirty_count();
    const int coalesced_count = static_cast<int>(regions.size());

    TileExecutionResult result;

    // PR-1 — use the authoritative scheduler's for_each_index() instead
    // of the raw tbb parallel_for.  The scheduler handles Sequential/TbbFixed/
    // TbbAutomatic modes internally.  When scheduler is null (test paths
    // without a wired scheduler), fall back to sequential execution.
    if (ctx.services.scheduler && parallel && coalesced_count > 1) {
        std::vector<uint64_t> per_region(regions.size(), 0);
        ctx.services.scheduler->for_each_index(
            regions.size(), [&](std::size_t i) {
                if (regions[i].is_empty()) return;
                auto region_result = execute_single_dirty_region(
                    compiled, ctx, sw_renderer, regions[i], output_fb, root_scope);
                per_region[i] = region_result.pixels_rendered;
            });
        for (auto px : per_region) {
            result.pixels_rendered += px;
        }
    } else {
        for (const auto& region : regions) {
            if (region.is_empty()) continue;
            auto region_result = execute_single_dirty_region(
                compiled, ctx, sw_renderer, region, output_fb, root_scope);
            result.pixels_rendered += region_result.pixels_rendered;
        }
    }

    // Report original tile count for performance counters.
    result.dirty_count = original_dirty;

    return result;
}

} // namespace chronon3d::graph::detail
