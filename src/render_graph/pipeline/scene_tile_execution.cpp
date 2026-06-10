#include "scene_tile_execution.hpp"

#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <algorithm>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>

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
    Framebuffer& output_fb
) {
    RenderGraphContext tile_ctx = ctx;
    tile_ctx.tile.clip_rect = region_bbox;
    tile_ctx.tile.dirty_rect = region_bbox;
    tile_ctx.options.reuse_prev_framebuffer = false;
    tile_ctx.tile.tile_execution_enabled = true;
    tile_ctx.tile.active_tile_clip = region_bbox;
    tile_ctx.options.skip_initial_clear = false;
    tile_ctx.tile.early_exit_skip.clear();

    FrameArena tile_arena;
    auto tile_fb = sw_renderer->executor()->execute(
        compiled, tile_ctx, &tile_arena);

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
    bool parallel
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

    if (parallel && coalesced_count > 1) {
        struct RegionAccum {
            uint64_t pixels_rendered{0};
        };
        tbb::enumerable_thread_specific<RegionAccum> accums;

        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, regions.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                auto& local = accums.local();
                for (size_t i = range.begin(); i < range.end(); ++i) {
                    const auto& region = regions[i];
                    if (region.is_empty()) continue;
                    auto region_result = execute_single_dirty_region(
                        compiled, ctx, sw_renderer, region, output_fb);
                    local.pixels_rendered += region_result.pixels_rendered;
                }
            }
        );

        for (const auto& acc : accums) {
            result.pixels_rendered += acc.pixels_rendered;
        }
    } else {
        for (const auto& region : regions) {
            if (region.is_empty()) continue;
            auto region_result = execute_single_dirty_region(
                compiled, ctx, sw_renderer, region, output_fb);
            result.pixels_rendered += region_result.pixels_rendered;
        }
    }

    // Report original tile count for performance counters.
    result.dirty_count = original_dirty;

    return result;
}

} // namespace chronon3d::graph::detail
