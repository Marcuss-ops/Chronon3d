#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_session_cache.hpp
//
// TICKET-031 — camera_session.hcomp.hpp
//
// Canonical strategy for stateful CameraConstraint pre-roll, fingerprint
// invalidation, Cut reset, and per-worker session isolation.
//
// The cache is the SOURCE OF TRUTH for `CameraSession` instances on a
// single worker (i.e. one render-job / one `std::thread`).  Two workers
// (or two independent `CameraSessionCache` instances) NEVER share state
// — the WP-3 isolation pattern from `RenderSession`/`SceneHasher`
// (see tests/runtime/test_render_session_reset_and_isolation.cpp) lifts
// directly here: each worker holds its own cache.
//
// Cache key:  `(program_id, shot_idx)`  — keyed by `program_id` (the
// fingerprint-stable descriptor.id) so a recompiled CameraProgram with
// the same id lands in the same entry; `shot_idx` keeps each shot
// isolated within a single timeline.  Program fingerprint is checked
// in-line on every acquire — mismatch forces full reset + re-preroll.
//
// Cut-reset on `transition_out == CameraTransitionKind::Cut`: when the
// timeline observes a Cut-exit before the next shot's first evaluate,
// `observe_cut_between(prior_shot_idx, next_shot_idx)` sets
// `cut_seen = true` on the prior-shot entry and `cut_seen = true` on
// the next-shot entry.  On the next acquire, `cut_seen = true` forces
// reset + re-preroll even when the fingerprint matches.
//
// The pre-roll procedure is deterministic: `preroll_session_for_frame`
// clears the session, then calls `program.evaluate(integral_ctx, sess)`
// for frames `max(shot_start, target - PREROLL_MAX) .. target - 1` so
// that stateful EMA-style constraints (currently the only one is
// `DampedFollowConstraint`) have populated EMA state at `target - 1`
// when the real call at `target` mutates it.
//
// Stateless programs short-circuit the pre-roll entirely (no per-frame
// state to warm up).  The fingerprint guard still fires to keep the
// stale-entry rejection uniform across the Stateless + RequiresHistory
// paths.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/core/types/sample_time.hpp>

#include <cstdint>
#include <unordered_map>

namespace chronon3d::camera_v1 {

// Forward declaration of the constraint state slot count (size of one
// DampedFollow EMA vector's worth).  Used by `preroll_session_for_frame`
// to size the session's constraint slots ONCE per program; once sized,
// subsequent calls reuse the same allocation.
struct CameraProgram;

// Forward declaration needed by CameraSessionLease's private constructor
// (which takes CameraSessionCache* before the class is defined below).
class CameraSessionCache;

/// Number of frames the canonical pre-roll procedure walks back when
/// priming a session for a stateful program.
///
/// Rationale: with `DampedFollowConstraint{damping=0.5f}` the EMA's
/// `previous_velocity` settles exponentially (factor `(1-damping)` per step).
/// At damping=0.5 the half-life is `ln(0.5)/ln(0.5) ≈ 1` frame; even at
/// the most aggressive damping in the canonical range (0.15 — see
/// `camera_descriptor.hpp::DampedFollowConstraint` default), the EMA reaches
/// 1% of its eventual asymptotic state in ~30 frames (1 - 0.85^30 ≈ 0.992).
/// 30 frames therefore covers the worst-case (`damping=0.15`) settle window
/// + a safety margin for any future stateful constraint that's added.
/// Configurable per call below for unit-test isolation.
constexpr int kCanonicalPrerollMaxFrames = 30;

// ============================================================================
// CameraSessionLease — RAII guard returned by CameraSessionCache::acquire().
//
// The lease holds a reference to the cached CameraSession.  The caller MUST
// call `commit()` after a successful `program.evaluate(ctx, lease.session())`
// to persist the session state and advance `last_evaluated_frame`.  If the
// lease is destroyed without `commit()`, the session state is discarded and
// `last_evaluated_frame` remains unchanged — protecting against exceptions,
// cancelled jobs, and forgotten release() calls.
//
// Usage:
//   auto lease = cache.acquire(program, shot_idx, shot_start, target, fps);
//   auto result = program.evaluate(ctx, lease.session());
//   if (result.ok) { lease.commit(); }
// ============================================================================

class CameraSessionLease {
public:
    CameraSession& session() { return *session_; }
    const CameraSession& session() const { return *session_; }

    /// Commit the post-evaluation session state back to the cache.
    /// Must be called exactly once after a successful evaluate().
    void commit();

    CameraSessionLease(const CameraSessionLease&) = delete;
    CameraSessionLease& operator=(const CameraSessionLease&) = delete;
    CameraSessionLease(CameraSessionLease&&) noexcept = default;
    CameraSessionLease& operator=(CameraSessionLease&&) noexcept = default;

    /// Destroy the lease.  TICKET-ZERO-A1 / TICKET-A3-CACHE-LEASE:
    /// rollback is BY CONSTRUCTION — lease.session() returns
    /// *Entry::working_session (a sibling field of Entry::checkpoint.session);
    /// commit() is the SOLE writepath that copies working_session →
    /// checkpoint.session.  When this destructor runs without commit()
    /// having been called (exception, cancelled job, forgotten release),
    /// the working_session scratch is dropped without writeback and
    /// checkpoint.session is left untouched.  No code runs here; the
    /// implicit rollback comes from the cache::acquire() rule.
    ~CameraSessionLease();

private:
    friend class CameraSessionCache;
    CameraSessionLease(CameraSessionCache* cache, int shot_idx,
                       CameraSession* session, int target_frame)
        : cache_(cache), shot_idx_(shot_idx),
          session_(session), target_frame_(target_frame) {}

    CameraSessionCache* cache_;
    int                 shot_idx_;
    CameraSession*      session_;
    int                 target_frame_;
    bool                committed_{false};
};

// ============================================================================
// CameraSessionCache — one-per-worker store of primed `CameraSession`s.
// ============================================================================

class CameraSessionCache {
public:
    CameraSessionCache() = default;
    CameraSessionCache(const CameraSessionCache&) = delete;
    CameraSessionCache& operator=(const CameraSessionCache&) = delete;

    /// Acquire (and prime) the session for `program` on `shot_idx`.  On
    /// entry the returned `CameraSessionLease` holds a guaranteed-valid
    /// session: either a freshly-reset+prerolled session (`cut_seen == true`
    /// OR fingerprint mismatch OR first-encounter) or a checkpoint-restored
    /// session.  `target_frame` is the absolute frame index the caller is
    /// about to evaluate.  `frame_rate` is the caller's project frame rate
    /// (required for deterministic pre-roll).
    ///
    /// The caller must call `lease.commit()` after a successful evaluate()
    /// to persist the session state and advance last_evaluated_frame.
    /// If the lease is destroyed without commit(), the session state is
    /// discarded.
    CameraSessionLease acquire(const CameraProgram& program,
                               int shot_idx,
                               int shot_start_frame,
                               int target_frame,
                               FrameRate frame_rate);

    /// Mark the connection between `prior_idx` and `next_idx` so that
    /// the next `acquire(next_idx, ...)` forces a full reset + preroll
    /// even if the current descriptor fingerprint matches.  Callers
    /// must invoke this when the timeline resolver observes a
    /// `CameraTransitionKind::Cut` between two shots.
    void observe_cut_between(int prior_idx, int next_idx);

    /// Look up an existing entry without priming.  Returns nullptr if the
    /// `(program, shot_idx)` tuple has never been primed on this worker.
    CameraStateCheckpoint* find(int shot_idx);

    /// Drop all primed sessions.  Useful at job boundaries and on
    /// schedule reconfig.  Per-worker invariant preserved: another
    /// worker's cache is untouched.
    void reset_all() { entries_.clear(); }

    /// Number of primed `(program_id, shot_idx)` entries.  Test-only
    /// surface (sized observability for worker isolation assertions).
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        CameraStateCheckpoint          checkpoint;

        // TICKET-ZERO-A1 / TICKET-A3-CACHE-LEASE — working session for
        // in-flight mutations under an active lease.  Allocated as a
        // plain value (NOT std::optional) so the in-place std::vector
        // capacity grows to `descriptor.constraints.size()` on the first
        // acquire and is REUSED across subsequent acquires.  Acquire()
        // copies `checkpoint.session` into `working_session` (capacity
        // reuse, 0 heap allocations per acquire); lease.session() returns
        // a reference to `working_session`; commit() copies
        // `working_session` back to `checkpoint.session`.  Uncommitted
        // leases implicitly rollback — the working_session scratch is
        // dropped along with the lease destructor and `checkpoint.session`
        // is untouched.  See ADR-013 Decision 3 + the regression lock in
        // tests/runtime/test_camera_session_cache_failed_no_commit_session_state.cpp.
        CameraSession                  working_session{};
    };

    // Keyed by shot_idx.  We don't include program_id because the cache
    // is one-per-(program, worker); a worker running a different
    // program gets a fresh cache.  Including shot_idx alone keeps the
    // map trivial and matches the per-shot pre-roll window model.
    std::unordered_map<int, Entry> entries_;

    friend class CameraSessionLease;

    /// Called by CameraSessionLease::commit() to finalise the lease.
    void commit_lease(int shot_idx, const CameraSession& session,
                      int target_frame);
};

// ============================================================================
// Free helper — canonical pre-roll procedure.
//
// Skip entirely for `Stateless` programs (no per-frame state to warm up).
// Otherwise, size constraint_state slots ONCE per program on first call,
// reset the session, then loop frames `max(shot_start, target - PREROLL_MAX)`
// .. `target - 1` calling `program.evaluate(...)` so that stateful
// constraints (currently `DampedFollowConstraint`) populate EMA state.
// Returns true if a pre-roll actually ran (i.e. the program was
// `RequiresHistory`); false if no-op (Stateless).  The function does
// NOT mutate `target_frame` semantics — the caller must still call
// `program.evaluate(actual_ctx, sess)` for `target_frame` after this
// returns.
// ============================================================================

[[nodiscard]] bool preroll_session_for_frame(const CameraProgram& program,
                                             int shot_start_frame,
                                             int target_frame,
                                             int preroll_max_frames,
                                             CameraSession& session,
                                             FrameRate frame_rate);

} // namespace chronon3d::camera_v1
