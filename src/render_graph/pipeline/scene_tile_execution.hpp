#pragma once

// ---------------------------------------------------------------------------
// scene_tile_execution.hpp
//
// Tile-based graph execution for dirty-tile partial rendering.
// Extracted from scene.cpp to keep that file focused on orchestration.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include <chronon3d/core/scope/execution_scope.hpp>   // PR 6.2 — root scope parameter
#include "scene_internal.hpp"
#include "tile_execution_policy.hpp"

namespace chronon3d { class SoftwareRenderer; }

namespace chronon3d::graph::detail {

struct TileExecutionResult {
    int dirty_count{0};
    int regions_executed{0};
    uint64_t pixels_rendered{0};
};

TileExecutionResult execute_dirty_tiles(
    CompiledFrameGraph& compiled,
    RenderGraphContext& ctx,
    SoftwareRenderer* sw_renderer,
    const FrameExecutionPlan& execution_plan,
    Framebuffer& output_fb,
    i32 width,
    i32 height,
    bool parallel,
    ExecutionScope& root_scope);

} // namespace chronon3d::graph::detail
