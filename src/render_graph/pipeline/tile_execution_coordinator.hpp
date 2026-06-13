#pragma once

// ---------------------------------------------------------------------------
// tile_execution_coordinator.hpp
//
/// @file    tile_execution_coordinator.hpp
/// @brief   Orchestrates tile-based vs traditional single-pass graph execution.
///          Encapsulates the tile decision, framebuffer allocation, tile
///          execution with counter updates, and the traditional fallback path.
///          Extracted from scene.cpp to keep the orchestrator clean.
//
// The coordinator replaces three inline blocks in scene.cpp:
//   1. TileExecutionPolicy::decide() call + diagnostics logging
//   2. Tile execution path (allocate FB, execute tiles, update counters)
//   3. Traditional fallback path (single-pass graph execute + tile fallback tracking)
// ===========================================================================

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include "scene_internal.hpp"
#include <memory>

namespace chronon3d {
class SoftwareRenderer;
}

namespace chronon3d::graph {

/// Result of a tile-or-traditional execution.
struct TileExecutionResult {
    std::shared_ptr<Framebuffer> fb;
    bool use_tile_execution{false};
};

/// Decide between tile-based and traditional execution, execute the graph,
/// and return the resulting framebuffer + execution mode.
/// Updates tile telemetry counters internally.
[[nodiscard]] TileExecutionResult execute_tile_or_fallback(
    RenderGraphContext& ctx,
    CompiledFrameGraph& compiled,
    const detail::LayerResolutionResult& resolved,
    const RenderSettings& settings,
    const detail::DirtyRectOutput& dirty_out,
    double dirty_ratio,
    SoftwareRenderer* sw_renderer,
    Frame frame,
    int width,
    int height);

} // namespace chronon3d::graph
