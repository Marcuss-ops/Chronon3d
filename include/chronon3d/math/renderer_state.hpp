#pragma once

// ---------------------------------------------------------------------------
// math/renderer_state.hpp
//
// WP-3 PR 3.2 — thin compatibility shim.
//
// The originally-included types (RendererFrameHistory, RendererDirtyTelemetry,
// RendererLayerHistory, LayerBBoxState) have all been either RENAMED to
// canonical names or FOLDED into a different canonical struct:
//
//   * `FrameHistory`            (was RendererFrameHistory)
//                                 → <chronon3d/runtime/frame_history.hpp>
//   * `DirtyHistory`            (was RendererDirtyTelemetry + folded
//                                 RendererLayerHistory::prev_layer_bboxes)
//                                 → <chronon3d/runtime/dirty_history.hpp>
//   * `LayerBBoxState`          — CANONICAL HOME is now
//                                 <chronon3d/runtime/dirty_history.hpp>
//                                 (was here pre-3.2; moved so that
//                                 `DirtyHistory::previous_layers<...>`
//                                 reaches `LayerBBoxState` without a
//                                 circular include against this header).
//   * `RendererLayerHistory`    — REMOVED entirely (folded into DirtyHistory).
//
// Backward-compat aliases (`using RendererFrameHistory = FrameHistory;`
// etc.) were removed in WP-3 PR 3.2 because:
//   - No external SDK consumer holds the legacy names (the public API
//     surface is Gated through `RenderSession`'s methods, not raw field
//     references).
//   - Internal call-sites are all transitioned to canonical names in
//     this PR.
//
// If you are adding a NEW history-related type: declare it in the
// canonical runtime/ header (frame_history.hpp or dirty_history.hpp),
// not here.  This shim is for backward-compatibility re-exports only
// and is a freeze surface; future development lives upstream.
// ---------------------------------------------------------------------------

#include <chronon3d/runtime/dirty_history.hpp>
#include <chronon3d/runtime/frame_history.hpp>
