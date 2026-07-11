// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_session_cache.cpp
//
// TICKET-031 — implementation of `CameraSessionCache` (per-worker canonical
// pre-roll + fingerprint invalidation + Cut reset) and the free helper
// `preroll_session_for_frame`.
//
// Determinism contract (per TICKET-031 spec):
//   - Stateless programs (`CameraProgram::evaluation_dependency() ==
//     Stateless`) skip the pre-roll entirely.
//   - Stateful programs: on first-encounter, fingerprint mismatch, or
//     Cut-seen marker, the session is reset and `preroll_session_for_frame`
//     walks `max(shot_start_frame, target_frame - PREROLL_MAX)` ..
//     `target_frame - 1` calling `program.evaluate(...)` so that
//     `DampedFollowConstraint`'s EMA state is fully primed for `target_frame`.
//   - Sequential forward walk: target_frame == last_evaluated_frame + 1 ⇒
//     cache reused without re-preroll (single-step caching).
//   - Random far-back traversal or fresh worker: re-pre-roll from
//     `max(shot_start, target - PREROLL_MAX)`. Reuses same code path as
//     first-encounter.
//
// Per-worker invariant: the `CameraSessionCache` is owned by exactly one
// worker; cross-worker sharing is a programmer error.  No mutex is taken
// inside the cache itself; thread safety is enforced by the WP-3
// `RenderSession` ownership convention.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_session_cache.hpp>

#include <algorithm>

namespace chronon3d::camera_v1 {

// ----------------------------------------------------------------------------
// Free helper — canonical pre-roll procedure.
// ----------------------------------------------------------------------------
bool preroll_session_for_frame(const CameraProgram& program,
                               int shot_start_frame,
                               int target_frame,
                               int preroll_max_frames,
                               CameraSession& session,
                               FrameRate frame_rate) {
    if (target_frame < shot_start_frame) {
        // Caller has supplied a target before the shot starts; treat as
        // "no pre-roll" — the first real evaluate will land at the
        // shot's start frame anyway.
        return false;
    }
    if (program.evaluation_dependency() ==
        CameraEvaluationDependency::Stateless) {
        // No per-frame state to warm up; skip pre-roll entirely.
        return false;
    }
    if (program.descriptor() == nullptr) {
        // Uncompiled program: skip pre-roll (a real evaluate would emit
        // Error diagnostics, the caller is responsible for handling them).
        return false;
    }

    // Ensure constraint slots are sized — the program evaluates against
    // the descriptor's constraint count on the first call.  Sizing here
    // eliminates per-frame reallocations across the pre-roll loop.
    session.ensure_constraint_states(program.descriptor()->constraints.size());
    session.reset();

    // First frame of the shot is always adjacent to shot.start_frame; the
    // hi (target-1) < lo (max(shot_start, target-30)) window becomes
    // empty there.  We still reset the session (above) so the EMA has
    // explicit `has_previous == false` semantics at first frame.
    const int lo = std::max(shot_start_frame, target_frame - preroll_max_frames);
    const int hi = target_frame - 1;
    if (hi < lo) {
        return true;  // stateful program but the window collapsed; reset still applied.
    }

    // Walk forward through the window.  Integrates with the EMA's
    // forward-only `previous_time` evolution.  Discard Camera2_5D + diag
    // results — we only care about ConstraintSession mutation.
    // CAM-05: FrameRate is explicit from the caller (no hardcoded 30 fps).
    for (int f = lo; f <= hi; ++f) {
        CameraEvalContext ctx = CameraEvalContext::at(Frame{f}, frame_rate);
        (void)program.evaluate(ctx, session);
    }
    return true;
}

// ----------------------------------------------------------------------------
// CameraSessionCache — per-worker.
// ----------------------------------------------------------------------------

CameraStateCheckpoint* CameraSessionCache::find(int shot_idx) {
    auto it = entries_.find(shot_idx);
    return it != entries_.end() ? &it->second.checkpoint : nullptr;
}

CameraSessionLease CameraSessionCache::acquire(const CameraProgram& program,
                                            int shot_idx,
                                            int shot_start_frame,
                                            int target_frame,
                                            FrameRate frame_rate) {
    const CameraDescriptor* desc = program.descriptor();
    const CameraDescriptorFingerprint dd_fp =
        desc ? compute_camera_descriptor_fingerprint(*desc) : 0;

    Entry& e = entries_[shot_idx];  // default-constructs on first encounter

    // Forward-step reuse optimization: when target == last_evaluated_frame + 1
    // AND the descriptor fingerprint matches AND no Cut marker is pending
    // AND the slot is valid, the EMA state has been primed exactly through
    // the prior frame's evaluate and we can fast-path the current eval.
    //
    // Every OTHER access pattern forces a full reset + canonical
    // pre-roll, guaranteeing bit-equal state for whichever access
    // pattern is used (first-encounter, fingerprint mismatch,
    // cut_seen, retry of same frame, random far-back traversal,
    // sub-frame access):
    //   - !valid            : first-encounter.
    //   - fp != dd.fp       : compiled program has new content (fingerprint
    //                         invalidation — program re-bound with same id
    //                         but different descriptor).
    //   - cut_seen          : timeline observed a Cut exit; reset + preroll.
    //   - target != last+1 : retry, random back-access, sub-frame repeat,
    //                         skip-ahead, etc.
    const bool forward_step =
        e.checkpoint.valid
        && e.checkpoint.descriptor_fingerprint == dd_fp
        && !e.checkpoint.cut_seen
        && target_frame == e.checkpoint.last_evaluated_frame + 1;
    const bool must_reprime = !forward_step;

    if (must_reprime) {
        e.checkpoint.session.reset();
        e.checkpoint.valid                  = true;
        e.checkpoint.descriptor_fingerprint = dd_fp;
        e.checkpoint.shot_start_frame       = shot_start_frame;
        e.checkpoint.cut_seen               = false;  // consume marker
        // return is informative only (primed? collapsed window?) — each
        // repprime path guarantees the same outcome so acquire doesn't
        // branch on it; discard the bool.
        (void)preroll_session_for_frame(program, shot_start_frame, target_frame,
                                        kCanonicalPrerollMaxFrames,
                                        e.checkpoint.session,
                                        frame_rate);
    }
    // CAM-05: last_evaluated_frame is now set by CameraSessionLease::commit(),
    // not eagerly here.  If the caller never calls commit() (exception,
    // cancelled job, forgotten release), the checkpoint is unchanged.
    //
    // TICKET-ZERO-A1 / TICKET-A3-CACHE-LEASE — copy the (possibly-prerolled)
    // cache state into the in-flight working_session.  Reuses the in-place
    // std::vector<ConstraintState> capacity grown by the preroll above (when
    // applicable) or by the forward_step's prior commit — heap allocations
    // per acquire are 0 after the first acquire on a given shot_idx.
    // lease.session() returns a reference to working_session; commit() copies
    // working_session back to checkpoint.session; uncommitted leases
    // implicitly rollback (no writeback — checkpoint.session is untouched).
    e.working_session = e.checkpoint.session;
    return CameraSessionLease(this, shot_idx,
                              &e.working_session, target_frame);
}


// ── CameraSessionLease ─────────────────────────────────────────────────────

void CameraSessionLease::commit() {
    if (!committed_) {
        committed_ = true;
        cache_->commit_lease(shot_idx_, *session_, target_frame_);
    }
}

// Phase 1.D (TICKET-PHASE-1D): commit-with-payload overload. Writes the given
// camera onto `session.last_valid_camera` (so the success-path payload joins
// the writeback) and then performs the no-arg commit() writeback in one
// atomic-then-commit step. The id-check on `committed_` is shared with
// no-arg commit() so calling either path on an already-finalized lease
// is a safe no-op (idempotent with the no-arg path).
void CameraSessionLease::commit(const Camera2_5D& cam) {
    if (!committed_) {
        session_->last_valid_camera = cam;
        commit();
    }
}

// Phase 1.D (TICKET-PHASE-1D): explicit rollback for the failed-evaluate branch.
// Mirrors the user-spec pattern `if (!result) { lease.rollback(); return ...; }`.
// Marking `committed_ = true` blocks any subsequent commit() from accidentally
// promoting the in-flight working_session back to checkpoint.session. The
// destructor remains a no-op (the implicit rollback-by-construction via
// scratch working_session still holds; we just synchronously finalize the
// lease so the failure branch is observable at the call site).
void CameraSessionLease::rollback() {
    committed_ = true;  // finalise without writeback; idempotent with commit().
}

CameraSessionLease::~CameraSessionLease() {
    // No-op.  TICKET-ZERO-A1 / TICKET-A3-CACHE-LEASE: rollback is BY
    // CONSTRUCTION — lease.session() returns *Entry::working_session
    // (a sibling scratch field on the cache's Entry, separate from
    // Entry::checkpoint.session).  commit() is the SOLE writepath that
    // copies working_session → checkpoint.session via commit_lease().
    // When this destructor runs without commit() having been called
    // (exception, cancelled job, forgotten release), the working_session
    // scratch is dropped along with the lease destructor and
    // checkpoint.session is left untouched.  No code is needed here —
    // the implicit rollback comes from the cache::acquire() contract
    // (lease points at the sibling working_session field, NOT at the
    // cache's committed state).
}

void CameraSessionCache::commit_lease(int shot_idx,
                                       const CameraSession& session,
                                       int target_frame) {
    auto it = entries_.find(shot_idx);
    if (it == entries_.end()) return;
    it->second.checkpoint.session = session;
    it->second.checkpoint.last_evaluated_frame = target_frame;
}

void CameraSessionCache::observe_cut_between(int prior_idx, int next_idx) {
    // Mark the prior-shot entry: its session state must be cleared so
    // any subsequent reference back to it (e.g. a debug-overlay read)
    // sees a fresh session.  In practice the timeline resolver only
    // reaches the prior-shot entry on the overlap window edge, so the
    // marker is mostly defensive.
    if (auto it = entries_.find(prior_idx); it != entries_.end()) {
        it->second.checkpoint.cut_seen = true;
    }
    // Mark the next-shot entry: its first evaluate after the Cut must
    // re-pre-roll even if the descriptor fingerprint is unchanged.
    if (auto it = entries_.find(next_idx); it != entries_.end()) {
        it->second.checkpoint.cut_seen = true;
    } else {
        // Insert a not-yet-valid marker so the next acquire() sees
        // `cut_seen == true` via the `must_reprime` rule above.
        Entry fresh;
        fresh.checkpoint.valid    = false;
        fresh.checkpoint.cut_seen = true;
        entries_.emplace(next_idx, fresh);
    }
}

} // namespace chronon3d::camera_v1
