#pragma once

// ---------------------------------------------------------------------------
// tile_execution_policy.hpp
//
// Encapsulates the decision of whether tile-based graph execution should be
// used for the current frame, and produces a human-readable reason when it
// is disabled.  Replaces a block of inline boolean logic in scene.cpp that
// had to be read end-to-end to understand why tile execution was skipped.
//
// Reasons produced (when disabled):
//   - "spatial_effect_detected"   : a layer has blur/glow/bloom/drop-shadow/distort/temporal
//   - "dirty_rects_not_active"    : dirty-rect tracking did not enable tile bitmask
//   - "dirty_ratio_too_high"      : dirty screen fraction > dirty.tile_dirty_ratio_threshold
//   - "missing_renderer_executor" : no SoftwareRenderer or no graph executor
//   - "no_dirty_tiles"            : no tile grid / mask available or no dirty tiles
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "scene_internal.hpp"
#include <string>

namespace chronon3d::effects {
    class EffectCatalog;
}

namespace chronon3d::graph {

/// Result of a tile-execution policy decision.  When `enabled` is false,
/// `reason_if_disabled` carries a stable snake_case token suitable for logs
/// and metrics.
struct TileDecision {
    bool enabled{false};
    std::string reason_if_disabled; // empty when enabled
};

/// TileExecutionPolicy decides whether tile-based graph execution should be
/// used for the current frame.  It is a static, pure function — the inputs
/// are everything it needs, so callers don't have to maintain state.
class TileExecutionPolicy {
public:
    /// Decide whether to use tile execution this frame.
    ///
    /// @param resolved      Resolved layer set for this frame.
    /// @param settings      Current render settings.
    /// @param dirty_out     Output of the dirty-rect phase (compute_dirty_rect).
    /// @param dirty_ratio   Fraction of screen covered by dirty pixels (0..1).
    /// @param sw_renderer   Renderer pointer (may be null).
    /// @param frame         Current frame number.
    static TileDecision decide(
        const detail::LayerResolutionResult& resolved,
        const RenderSettings& settings,
        const detail::DirtyRectOutput& dirty_out,
        double dirty_ratio,
        const SoftwareRenderer* sw_renderer,
        Frame frame,
        const effects::EffectCatalog* effect_catalog = nullptr
    );
};



} // namespace chronon3d::graph
