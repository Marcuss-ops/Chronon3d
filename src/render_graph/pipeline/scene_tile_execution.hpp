#pragma once

// ---------------------------------------------------------------------------
// scene_tile_execution.hpp
//
// Tile-based graph execution for dirty-tile partial rendering.
// Extracted from scene.cpp to keep that file focused on orchestration.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include "scene_internal.hpp"

namespace chronon3d::graph::detail {

struct TileExecutionResult {
    int dirty_count{0};
    uint64_t pixels_rendered{0};
};

// ── Tile coalescing ───────────────────────────────────────────────────────
//
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
    const raster::DirtyTileMask& mask);

TileExecutionResult execute_dirty_tiles(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer,
    const DirtyRectOutput& dirty_out,
    Framebuffer& output_fb,
    i32 width,
    i32 height,
    bool parallel);

} // namespace chronon3d::graph::detail
