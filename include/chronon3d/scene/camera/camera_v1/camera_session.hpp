#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_session.hpp
//
// CameraSession — mutable per-evaluation session, separated from constraints.
//
// The session owns the ConstraintSession (pre-allocated per-constraint state
// slots) and the banking roll accumulator.  The compiler pre-allocates exactly
// the number of constraint slots needed so there is no allocation in the hot
// path.
//
// Usage (compiled path):
//   CameraSession session;
//   session.ensure_constraint_states(program.num_constraints());
//   auto result = program.evaluate(ctx, session);
//
// Usage (builder path — unchanged):
//   ConstraintSession session;
//   session.ensure_states(prog.constraints_.size());
//   auto result = prog.evaluate(ctx, session);
//
// ── TICKET-031 ─────────────────────────────────────────────────────────────────
// `CameraStateCheckpoint` — a by-value snapshot of a `CameraSession` plus the
// provenance needed for the canonical pre-roll procedure:
//   - `descriptor_fingerprint` — the FNV-1a hash of the descriptor the
//     session was warmed from; on mismatch, the cache entry is invalidated
//     and the session is re-primed.
//   - `last_evaluated_frame` — the last absolute frame whose evaluation
//     contributed to the snapshot; used to compute the pre-roll window
//     (we re-prime from `max(shot_start_frame, last_evaluated_frame - PREROLL_MAX)`
//      to `last_evaluated_frame` when reusing).
//   - `shot_start_frame` — the shot's `start_frame` so the cache can
//     bound the pre-roll window when the snapshot is restored on a
//     different shot index.
//   - `cut_seen` — a per-shot marker set when the timeline resolver
//     observes a `CameraTransitionKind::Cut` exit between this shot and
//     the next; forces a full reset + pre-roll on next evaluate.
//   - `valid` — false until `capture(...)` materialises the entry for
//     the first time; loaders must treat invalid checkpoints as
//     "first-encounter → pre-roll from shot.start_frame".
//
// The canonical eviction / restore contract:
//   - `capture(session, descriptor, frame, shot_start)`  → snapshot
//   - `restore(session, checkpoint)`                      → bit-exact copy back
//   - Snapshot is plain-old-data + the by-value `CameraSession`; no
//     heap pointers leak out, so the struct is safe to copy around
//     between workers (no shared mutable state beyond what `CameraSession`
//     already carries).
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_constraint.hpp>  // ConstraintSession, ConstraintState
#include <chronon3d/math/glm_types.hpp>   // Vec3

#include <cstdint>
#include <optional>

namespace chronon3d::camera_v1 {

/// Top-level session for the compiled camera evaluation path.
/// Wraps constraint state + banking roll + any future per-program state.
struct CameraSession {
    /// Constraint state slots (one per constraint, index-aligned).
    ConstraintSession constraint_session;

    /// Banking roll accumulator (shared state across frames).
    float banking_roll{0.0f};

    /// CAM-03 / DOC 02: when both an OrientationSpec look-at AND a
    /// LookAtConstraint are present in the descriptor, the orientation
    /// wins (it carries the source-derived target).  This flag is set by
    /// CameraProgram::evaluate() and consumed by the constraint loop so
    /// the constraint's look-at branch is skipped — preserving the
    /// single-look-at policy.
    bool skip_look_at_constraint_from_orientation{false};

    /// Last valid trajectory tangent (normalised) — used by OrientAlongPath
    /// as a fallback when the current frame's tangent is degenerate.
    std::optional<Vec3> last_tangent;

    /// Ensure at least n constraint state slots are allocated.
    void ensure_constraint_states(std::size_t n) {
        constraint_session.ensure_states(n);
    }

    /// Reset all session state (call at the start of a new render).
    void reset() {
        constraint_session.reset();
        banking_roll = 0.0f;
        skip_look_at_constraint_from_orientation = false;
        last_tangent.reset();
    }
};

// ============================================================================
// CameraStateCheckpoint — by-value snapshot of a session + provenance
// (TICKET-031 — canonical pre-roll + fingerprint invalidation + Cut reset).
// ============================================================================

/// Type of the descriptor fingerprint the cache stores alongside each entry.
using CameraDescriptorFingerprint = std::uint64_t;

/// Protobuf-style snapshot of a session.  Plain-old-data + a by-value
/// `CameraSession`.  Default member initializers preserve a valid
/// "uninitialised" sentinel (`valid == false`) that loaders must reset
/// before reuse.  Copy semantics are bit-exact because every contained
/// type is either a by-value aggregate or a `std::vector` (deep copy).
struct CameraStateCheckpoint {
    CameraSession         session{};
    CameraDescriptorFingerprint descriptor_fingerprint{0};
    int                   last_evaluated_frame{-1};   ///< abs frame index in timeline
    int                   shot_start_frame{0};       ///< shot.start_frame at capture time
    bool                  cut_seen{false};           ///< Cut-exit observed before restore
    bool                  valid{false};              ///< false until first capture()

    /// Capture the current session (by-value snapshot).  `descriptor_fp` is
    /// typically `compute_camera_descriptor_fingerprint(*prog.descriptor())`;
    /// it travels with the checkpoint so the cache can reject stale entries
    /// whose descriptor has changed (fingerprint invalidation).
    static CameraStateCheckpoint capture(const CameraSession& s,
                                        CameraDescriptorFingerprint descriptor_fp,
                                        int last_evaluated_frame,
                                        int shot_start_frame) {
        CameraStateCheckpoint cp;
        cp.session                 = s;
        cp.descriptor_fingerprint  = descriptor_fp;
        cp.last_evaluated_frame    = last_evaluated_frame;
        cp.shot_start_frame        = shot_start_frame;
        cp.cut_seen                = false;
        cp.valid                   = true;
        return cp;
    }

    /// Bit-exact copy back into a session.  After restore the caller has
    /// the state of the session at the time of capture (modulo any
    /// subsequent evaluations the caller may have performed).  No side
    /// effects on `cp`.
    static void restore(CameraSession& s, const CameraStateCheckpoint& cp) {
        s = cp.session;
    }

    /// Mark this checkpoint for full-reset on the next prime: the timeline
    /// resolver will call this when it observes a `CameraTransitionKind::Cut`
    /// exit between the prior shot and the shot whose checkpoint this
    /// belongs to.  The cache honors the marker on the next lookup.
    void mark_cut_seen() { cut_seen = true; }
};

} // namespace chronon3d::camera_v1
