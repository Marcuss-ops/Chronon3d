#pragma once

// ---------------------------------------------------------------------------
// math/renderer_state.hpp
//
// Backward-compatibility header for the renderer-agnostic history /
// telemetry structs.
//
// Originally this header defined `RendererFrameHistory`,
// `RendererDirtyTelemetry`, `RendererLayerHistory`, and `LayerBBoxState`
// directly.  After the RenderSession extraction refactor:
//   - `FrameHistory` (formerly `RendererFrameHistory`) is defined in
//     `<chronon3d/runtime/frame_history.hpp>`.
//   - `DirtyHistory` (formerly `RendererDirtyTelemetry`) is defined in
//     `<chronon3d/runtime/dirty_history.hpp>`.
//   - `LayerBBoxState` and `RendererLayerHistory` still live here for
//     now: the latter is *not yet* merged into `DirtyHistory` (the merge
//     is the end-state target of the refactor; left for a later phase so
//     call-sites that read `session.layer_history().prev_layer_bboxes`
//     continue to work).
//
// This file is intentionally kept thin: it includes the canonical
// headers and re-exposes the legacy type names as `using` aliases, plus
// the two remaining types that haven't migrated yet.
//
// SoftwareRenderer still composes from these types via inclusion — its
// public accessor methods (frame_history(), dirty_telemetry(),
// layer_history()) are unchanged.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/core/types/frame.hpp>

// Canonical definitions (Phase 2 — live in runtime/).
#include <chronon3d/runtime/frame_history.hpp>
#include <chronon3d/runtime/dirty_history.hpp>

namespace chronon3d {

// ── Backward-compatibility aliases ──────────────────────────────────────
//
// Existing call-sites that still reference `RendererFrameHistory` or
// `RendererDirtyTelemetry` continue to work because both names are the
// same struct as `FrameHistory` / `DirtyHistory` respectively (one struct,
// two names — `using` alias, not a distinct type).
//
// ODR-safe: the canonical struct is defined exactly once, in
// `runtime/frame_history.hpp` / `runtime/dirty_history.hpp`.
using RendererFrameHistory = ::chronon3d::FrameHistory;
using RendererDirtyTelemetry = ::chronon3d::DirtyHistory;

// ── Still defined here for now (Phase 3+: LayerBBoxState stays;
// RendererLayerHistory is fanned out into DirtyHistory.previous_layers) ──

/// Per-layer bounding box + diff state for dirty-rect tracking.
///
/// Each active resolved layer produces one LayerBBoxState per frame.
/// The dirty-rect system diffs current vs. previous states to compute
/// the union of regions that need re-rendering.
struct LayerBBoxState {
    raster::BBox bbox;
    Mat4 world_matrix;
    f32 opacity{1.0f};
    bool visible{true};
    bool cache_static{false};
    bool uses_2_5d_projection{false};
    uint64_t content_hash{0};
};

/// Per-layer bbox history for dirty-rect frame-to-frame diffing.
///
/// Stores the previous frame's bbox state for every active layer so
/// the dirty-rect system can detect added, removed, moved, and
/// content-changed layers.
///
/// NOTE: this struct is the LAST unmerged history type.  The end-state
/// of the RenderSession extraction folds `prev_layer_bboxes` into
/// `DirtyHistory` under a `previous_layers` (or similarly-named) field.
struct RendererLayerHistory {
    std::unordered_map<std::string, LayerBBoxState> prev_layer_bboxes;
};

} // namespace chronon3d
