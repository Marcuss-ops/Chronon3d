#pragma once

// ---------------------------------------------------------------------------
// scene_tile_execution.hpp
//
// Tile-based graph execution for dirty-tile partial rendering.
// Header-only (inline) — extracted from scene.cpp to keep that file
// focused on orchestration.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <algorithm>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include "scene_internal.hpp"
// TBB dependency for parallel tile execution
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>

namespace chronon3d::graph::detail {

struct TileExecutionResult {
    int dirty_count{0};
    uint64_t pixels_rendered{0};
};

/// Execute a single dirty tile: set up tile context, run graph, copy result to output.
[[nodiscard]] static inline TileExecutionResult execute_single_dirty_tile(
    CompiledFrameGraph& compiled,
    const RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer,
    const raster::BBox& tile_bbox,
    Framebuffer& output_fb
) {
    RenderGraphContext tile_ctx = ctx;
    tile_ctx.clip_rect = tile_bbox;
    tile_ctx.dirty_rect = tile_bbox;
    tile_ctx.reuse_prev_framebuffer = false;
    tile_ctx.tile_execution_enabled = true;
    tile_ctx.active_tile_clip = tile_bbox;
    tile_ctx.skip_initial_clear = false;
    tile_ctx.early_exit_skip.clear();

    FrameArena tile_arena;
    auto tile_fb = sw_renderer->executor()->execute(
        compiled, tile_ctx, &tile_arena);

    if (tile_fb) {
        for (i32 y = tile_bbox.y0; y < tile_bbox.y1; ++y) {
            std::copy(
                tile_fb->pixels_row(y) + tile_bbox.x0,
                tile_fb->pixels_row(y) + tile_bbox.x1,
                output_fb.pixels_row(y) + tile_bbox.x0);
        }
    }

    return TileExecutionResult{
        .dirty_count = 1,
        .pixels_rendered =
            static_cast<uint64_t>(tile_bbox.x1 - tile_bbox.x0) *
            static_cast<uint64_t>(tile_bbox.y1 - tile_bbox.y0)
    };
}

inline TileExecutionResult execute_dirty_tiles(
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

    struct DirtyTile { int tx; int ty; };
    std::vector<DirtyTile> dirty_coords;
    dirty_coords.reserve(static_cast<size_t>(dirty_tiles.dirty_count()));
    dirty_tiles.for_each_dirty_tile(tile_grid, [&](int tx, int ty) {
        dirty_coords.push_back({tx, ty});
    });

    TileExecutionResult result;

    if (parallel) {
        struct TileAccum {
            int dirty_count{0};
            uint64_t pixels_rendered{0};
        };
        tbb::enumerable_thread_specific<TileAccum> tile_accums;

        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, dirty_coords.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                auto& local = tile_accums.local();
                for (size_t i = range.begin(); i < range.end(); ++i) {
                    const int tx = dirty_coords[i].tx;
                    const int ty = dirty_coords[i].ty;
                    const raster::BBox tile_bbox = tile_grid.tile_bounds(tx, ty);
                    if (tile_bbox.is_empty()) continue;

                    auto tile_result = execute_single_dirty_tile(
                        compiled, ctx, sw_renderer, tile_bbox, output_fb);
                    local.dirty_count += tile_result.dirty_count;
                    local.pixels_rendered += tile_result.pixels_rendered;
                }
            }
        );

        for (const auto& acc : tile_accums) {
            result.dirty_count += acc.dirty_count;
            result.pixels_rendered += acc.pixels_rendered;
        }
    } else {
        for (size_t i = 0; i < dirty_coords.size(); ++i) {
            const int tx = dirty_coords[i].tx;
            const int ty = dirty_coords[i].ty;
            const raster::BBox tile_bbox = tile_grid.tile_bounds(tx, ty);
            if (tile_bbox.is_empty()) continue;

            auto tile_result = execute_single_dirty_tile(
                compiled, ctx, sw_renderer, tile_bbox, output_fb);
            result.dirty_count += tile_result.dirty_count;
            result.pixels_rendered += tile_result.pixels_rendered;
        }
    }

    return result;
}

} // namespace chronon3d::graph::detail
