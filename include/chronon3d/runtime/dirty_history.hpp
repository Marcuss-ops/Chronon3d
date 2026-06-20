#pragma once

// ---------------------------------------------------------------------------
// runtime/dirty_history.hpp
//
// Per-frame dirty-rect / tiling / reuse telemetry — canonical definition.
//
// Migrated out of `math/renderer_state.hpp` as part of the RenderSession
// extraction (see "Spostare e separare RenderSession", section 8.4).
// Pre-Phase-2 this header was a `using` alias on top of
// `RendererDirtyTelemetry`; from Phase 2 onward the canonical struct lives
// here, and `math/renderer_state.hpp` exposes a backward-compatibility
// alias `RendererDirtyTelemetry = ::chronon3d::DirtyHistory`.
//
// Field names are preserved verbatim from the legacy `RendererDirtyTelemetry`
// so that the existing call-sites do not need to change.
//
// NOTE: the end-state of the RenderSession extraction also absorbs the
// per-layer bbox history currently in `RendererLayerHistory` (kept in
// `math/renderer_state.hpp` for now) into `DirtyHistory`.  That merge is
// left for a later phase.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <optional>

#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d {

/// Telemetry counters for dirty-rect / tile-execution decisions.
///
/// Populated by render_scene_via_graph() after each frame and queried
/// by the CLI (e.g. dry-run, telemetry report).
struct DirtyHistory {
    double last_dirty_area_ratio{1.0};
    int last_layer_count{0};
    bool last_dirty_rect_enabled{false};
    std::optional<raster::BBox> last_dirty_rect;
    bool last_tile_execution_used{false};
    bool last_fast_path_reused{false};
    bool last_graph_reused{false};
};

} // namespace chronon3d
