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
                               CameraSession& session) {
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
    // (CameraEvalContext::at(Frame) defaults to FrameRate{30,1} which is
    // what the pre-roll uses; no explicit `sample_time` override needed.)
    for (int f = lo; f <= hi; ++f) {
        CameraEvalContext ctx = CameraEvalContext::at(Frame{f});
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

CameraSession& CameraSessionCache::acquire(const CameraProgram& program,
                                            int shot_idx,
                                            int shot_start_frame,
                                            int target_frame) {
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
                                        e.checkpoint.session);
    }
    e.checkpoint.last_evaluated_frame = target_frame;
    return e.checkpoint.session;
}

void CameraSessionCache::release(int shot_idx,
                                 const CameraSession& sess) {
    auto it = entries_.find(shot_idx);
    if (it == entries_.end()) return;
    // Capture the post-eval session (so the next acquire / forward-step or
    // repprime has the up-to-date EMA state to work from).  The slot's
    // `last_evaluated_frame` is the sole responsibility of `acquire` — no
    // second writer here, eliminating silent drift.
    it->second.checkpoint.session = sess;
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
