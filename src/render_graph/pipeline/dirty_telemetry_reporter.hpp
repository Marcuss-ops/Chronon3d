#pragma once

// ---------------------------------------------------------------------------
// dirty_telemetry_reporter.hpp
//
/// @file    dirty_telemetry_reporter.hpp
/// @brief   Dirty-rect metrics computation, debug logging, and telemetry
///          recording.  Extracted from scene.cpp to keep the orchestrator
///          focused on pipeline flow.
//
// Functions:
//   compute_and_apply_dirty_metrics() — dirty ratio + union area → counters/renderer
//   log_dirty_debug()                 — per-frame dirty rect diagnostics
//   record_dirty_telemetry()          — end-of-frame dirty tracking stats
// ===========================================================================

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include "scene_internal.hpp"

namespace chronon3d {
class SoftwareRenderer;
}

namespace chronon3d::graph {

/// Compute dirty metrics (ratio, union area) from the dirty rect output,
/// store in telemetry counters and renderer dirty state.
/// Returns the computed dirty_ratio (0..1).
[[nodiscard]] double compute_and_apply_dirty_metrics(
    const detail::DirtyRectOutput& dirty_out,
    int width,
    int height,
    RenderCounters* counters,
    SoftwareRenderer* sw_renderer);

/// Log dirty-rect debug information when diagnostics are enabled.
void log_dirty_debug(
    SoftwareRenderer* sw_renderer,
    bool diagnostics_enabled,
    const detail::DirtyRectOutput& dirty_out,
    Frame frame);

/// Record per-frame dirty-rect telemetry on the renderer
/// (update_dirty_telemetry with success/failure counters).
void record_dirty_telemetry(
    SoftwareRenderer* sw_renderer,
    const detail::DirtyRectOutput& dirty_out,
    bool use_tile_execution,
    bool graph_reused);

} // namespace chronon3d::graph
