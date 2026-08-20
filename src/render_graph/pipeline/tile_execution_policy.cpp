#include "tile_execution_policy.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d::graph {

TileDecision TileExecutionPolicy::decide(
    const detail::LayerResolutionResult& resolved,
    const RenderSettings& settings,
    const detail::DirtyRectOutput& dirty_out,
    double dirty_ratio,
    const SoftwareRenderer* sw_renderer,
    Frame frame,
    const effects::EffectCatalog* effect_catalog
) {
    // Tile execution is incompatible with spatial effects (glow, bloom, drop shadow,
    // blur, distort, temporal).  When tile execution re-executes the full graph per
    // tile, the effect stack runs with a tile-scoped clip_rect.  The blur kernel in
    // these effects reads pixels outside the tile boundary — those pixels are zero
    // or garbage (from the fresh per-tile framebuffer), producing visible seams at
    // tile edges.  Disable tile execution when ANY active layer has spatial effects.
    if (detail::has_layer_with_spatial_effects(resolved, frame, effect_catalog)) {
        return {false, "spatial_effect_detected"};
    }

    // The dirty-rect phase must have enabled the bitmask-based tile output.
    if (!dirty_out.use_dirty_tiles) {
        return {false, "dirty_rects_not_active"};
    }

    // When the dirty area covers >threshold of the screen, per-tile graph
    // re-execution overhead dominates and single-pass is faster.  This threshold
    // prevents pathological slowdowns on scenes with large dirty regions.
    // Keep tile execution selected for high dirty ratios as well.  Switching
    // execution models mid-sequence invalidates the previous-frame restore
    // contract and produces observable gaps at tile boundaries.

    // Need a renderer + runtime to run the per-tile graph re-executions.
    // Section 5 violation fix: we no longer expose a public `executor()`
    // accessor on SoftwareRenderer; the executor lives on RenderRuntime.
    // `has_runtime()` is the equivalent pre-condition for callers.
    if (!sw_renderer || !sw_renderer->has_runtime()) {
        return {false, "missing_renderer_runtime"};
    }

    // Need a tile grid + mask with at least one dirty tile.
    if (!dirty_out.tile_grid || !dirty_out.dirty_tiles || !dirty_out.dirty_tiles->any()) {
        return {false, "no_dirty_tiles"};
    }

    // Use measured execution cost once both paths have enough samples.  The
    // safety margin prevents oscillation around equal timings; before the
    // model is ready, the existing correctness gates remain authoritative.
    const auto& cost = sw_renderer->dirty_telemetry();
    if (cost.tile_cost_model_ready() &&
        cost.tile_exec_ms_ewma > cost.full_frame_exec_ms_ewma * 1.10) {
        return {false, "cost_model_tile_slower"};
    }

    return {true, ""};
}

} // namespace chronon3d::graph
