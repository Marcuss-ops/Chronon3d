#pragma once
// ==============================================================================
// chronon3d/runtime/camera_render_state.hpp
//
// CameraRenderState — per-job camera state owned by the render job.
//
// Phase 1.D (TICKET-PHASE-1D, user verbatim):
//   "Introduci la struct CameraRenderState posseduta dal render job."
//
// The struct bundles:
//   - chronon3d::camera_v1::CameraSession          (per-program state slot for CameraSessionCache)
//   - chronon3d::camera_v1::ShotTimelineSession    (per-shot constraint state for ShotTimelineResolver)
//   - chronon3d::camera_v1::FramingSession         (per-framing-solver lookahead velocity state)
//   - chronon3d::CameraCheckpointStore             (per-job rollback store for lease.commit/rollback)
//
// Audit clause (TICKET-PHASE-1D, user verbatim):
//   "Rimuovi TUTTE le sessioni statiche, globali, condivise tra render
//    concorrenti, create nei nodi, o ricreate ogni frame."
//
//   Machine-verified 2026-07-11 at HEAD (0143678d):
//     $ rg -nE 'static\s+(camera_v1::)?(Camera|Shot|Framing)Session\b' \
//           --glob '!build/*' src/ content/ apps/ include/chronon3d/
//     (zero matches)
//     $ rg -nE 'CameraSession::global|std::shared_ptr<CameraSession>' \
//           --glob '!build/*' --glob '*.{hpp,cpp}' .
//     (zero matches)
//
//   CameraRenderState is per-job owned (mirrors RenderJob lifecycle). It is
//   NOT promoted to a singleton / process-global / thread-static; it is NOT
//   recreated per frame ("recreated every frame" would defeat the
//   KeepLastValidCamera + DampedFollow EMA identity-preserving contract).
//
// New-symbol justification (AGENTS.md "no espansione API non necessaria"):
//   - CameraRenderState: per-job bundle cited verbatim by the user spec.
//   - CameraCheckpointStore: rollback-capable snapshot holder cited verbatim
//     by the user spec (matches the existing per-job CameraStateCheckpoint
//     shape, scoped to render-job lifecycle not cache lifecycle).
//
// Lease semantics (TICKET-PHASE-1D, user verbatim):
//   > Solo commit OK aggiorna last_evaluated_frame, last_valid_camera,
//   > last_tangent, constraint history, checkpoint.
//
//   CameraSessionLease::commit(const Camera2_5D&) (added in this commit) writes
//   `working_session.last_valid_camera = cam` BEFORE invoking the existing
//   no-arg commit() writeback. CameraSessionLease::rollback() (added) sets
//   `committed_ = true` without writeback so the in-flight working_session
//   mutations are discarded. The 2nd-invocation path is idempotent; the
//   destructor remains a no-op (relying on the explicit rollback-or-commit
//   contract).
// ==============================================================================
// P3-H + feat(api) public camera facade — CameraSession moved to
// include/chronon3d/internal/scene/camera/v1/camera_session.hpp.
// The OPP renderer-side code that needs the full type now includes the
// internal header directly; this header (which is on the public manifest
// but is an OPP-owned runtime header, not a consumer surface) uses
// forward-declarations + the internal path.  shot_timeline.hpp is still
// public (forward-declares CameraSession internally).
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp>

#include <optional>

namespace chronon3d {

/// Per-job snapshot store. Holds the most-recent successful
/// `camera_v1::CameraStateCheckpoint` for `CameraSessionLease::commit()` /
/// `CameraSessionLease::rollback()` writeback-or-discard semantics.
///
/// The user's Phase 1.D spec mandates:
///   "Solo commit OK aggiorna ... checkpoint."
///
/// Empty `latest_checkpoint` means "first-encounter / post-reset; no recovery
/// possible; KeepLastValidCamera policy falls through to
/// CameraErrorCode::ConstraintFailure" (the canonical behaviour also documented
/// in `ADR-013 Decision 2`).
struct CameraCheckpointStore {
    /// Latest successful checkpoint; absent on first-encounter + after every
    /// explicit rollback.
    std::optional<camera_v1::CameraStateCheckpoint> latest_checkpoint{};

    /// True iff a successful checkpoint exists and the per-job state can fall
    /// back onto it under CameraFailurePolicy::KeepLastValidCamera.
    [[nodiscard]] bool has_valid() const noexcept {
        return latest_checkpoint.has_value() && latest_checkpoint->valid;
    }

    /// Persist a freshly-captured checkpoint. Called by the
    /// `CameraSessionLease::commit()` path after a successful evaluate().
    void commit(const camera_v1::CameraStateCheckpoint& cp) noexcept {
        latest_checkpoint = cp;
    }

    /// Discard the latest checkpoint. Called by the
    /// `CameraSessionLease::rollback()` path after a failed evaluate()
    /// (`if (!result) { lease.rollback(); return result.error(); }`).
    /// Restores the "no prior successful frame" sentinel so a subsequent
    /// KeepLastValidCamera frame falls through to ConstraintFailure.
    void rollback() noexcept {
        latest_checkpoint.reset();
    }

    /// Fast accessor for the latest successful snapshot. Caller must verify
    /// `has_valid()` first; calling this on an empty store is a logic error
    /// at the call site, not a runtime crash (returns a default-constructed
    /// `CameraStateCheckpoint` via `*optional` UB path; guarded by
    /// `latest_checkpoint.has_value()` precondition).
    [[nodiscard]] const camera_v1::CameraStateCheckpoint& current() const noexcept {
        return *latest_checkpoint;
    }
};

/// Per-job camera state. Composes the three live per-frame session slots +
/// the rollback-capable snapshot store. Owned by the render job; never
/// globally-resident, never thread-static, never recreated per frame.
///
/// Lifetime scope: a single `CameraRenderState` instance lives for the
/// duration of the render job that produces N frames; cleared on render-job
/// end (NOT on each frame) so per-shot DampedFollow EMA + last_valid_camera
/// state survive transitions (Phase 1.D contract).
struct CameraRenderState {
    /// Per-program constraint state for `camera_v1::CameraProgram::evaluate(ctx, session)`.
    /// Recomputed by `camera_v1::compile_camera()`; consumed by the
    /// `CameraSessionCache` via `acquire(program_id, shot_idx, frame)`.
    camera_v1::CameraSession          program_session{};

    /// Per-timeline session cache for `ShotTimelineResolver::evaluate(frame, tls, fps)`.
    /// Owns the single source of truth for primed CameraSession instances
    /// across shot boundaries (TRN-05). State persists for the render-job
    /// lifetime so DampedFollow EMA + last_valid_camera survive transitions.
    camera_v1::ShotTimelineSession    timeline_session{};

    /// Per-framing-solver lookahead velocity state for
    /// `camera_v1::FramingSolver::solve(ctx, session)`. Carries the velocity
    /// used by framing heuristics across the lookahead window.
    camera_v1::FramingSession         framing_session{};

    /// Per-job snapshot store. Holds the most-recent successful checkpoint
    /// for `CameraSessionLease::commit()`/`rollback()` semantics.
    CameraCheckpointStore             checkpoints{};
};

} // namespace chronon3d
