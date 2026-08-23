// ===========================================================================
// src/runtime/render_session.cpp
//
// WP-3 PR 3.1 + 3.2 — body of `RenderSession::reset_job()` and the shared
// reset helpers.
//
// Per-session ownership note (PR 3.1):
//
// scene_hasher + program_store are now value / unique_ptr members on
// RenderSession itself.  reset_job() reaches them directly — no proxy,
// no throw path.
//
// History consolidation (PR 3.2):
//
// `RendererLayerHistory` is gone.  Its payload (`prev_layer_bboxes`)
// has been folded into `DirtyHistory::previous_layers`.  `reset_job()`
// therefore clears `dirty_telemetry.previous_layers` so a full job
// reset wipes the wrapping-history payload as part of the per-session
// reset sequence.
//
// Bodies of the accessors (`scene_hasher()` / `program_store()`) are
// inline in the header — they return references to the local members;
// they cannot return null because the default ctor eagerly constructs
// `program_store` via `std::make_unique`.
// ===========================================================================

#include <chronon3d/internal/runtime/render_session.hpp>

namespace chronon3d {

void RenderSession::reset_temporal_history() {
    // Temporal state is deliberately independent from frame values and
    // compiled topology. Resetting it must not evict either runtime cache.
    frame_history = FrameHistory{};
    dirty_telemetry.previous_layers.clear();
}

void RenderSession::reset_scene_state() noexcept {
    scene_hasher_state = chronon3d::graph::SceneHasher{};
}

void RenderSession::reset_job() {
    // Frame-scoped telemetry counter first (folded PR 3.2 — the
    // reset_frame_temporaries body still lives inline in the header
    // because it cannot use a default-construction idiom: canonical
    // DirtyHistory has no user-defined ctor, just per-field defaults).
    reset_frame_temporaries();

    // Per-session temporal history reset is kept separate from frame-value
    // state so callers can choose the required invalidation domain.
    reset_temporal_history();

    // Per-session scene-hasher reset (PR 3.1 — moved from runtime-owned
    // to per-session-owned). The reset reaches only THIS session's local
    // instance; two concurrent sessions never clear each other's state.
    reset_scene_state();

    // Frame-value state is reset independently from temporal history.
    reset_frame_values();
    prepared_composition = nullptr;
}

} // namespace chronon3d
