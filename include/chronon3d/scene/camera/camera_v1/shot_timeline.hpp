#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/shot_timeline.hpp
//
// ShotTimeline — ordered sequence of CameraShots with transitions.
//
// A ShotTimeline resolves which shot is active at any given frame and
// evaluates the transition between consecutive shots (Cut, SmoothBlend,
// Push, WhipPan, FocusHandoff).
//
// Architecture:
//   CameraShot           → start/end frames + CameraProgram reference
//   CameraTransition     → pure function: interpolate from→to camera
//   ShotTimeline         → ordered list, find active shot, validate structure
//   ShotTimelineResolver → evaluate camera at frame, apply transitions
//   ShotTimelineSession  → per-shot persistent constraint state
//   CameraTransitionCatalog → registry of transition factories (DI, not singleton)
// ==============================================================================
#include <chronon3d/core/types/sample_time.hpp>  // FrameRate (TICKET-A3-CTX-FRAMERATE)
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::camera_v1 {

// =========================================================================
// Transition kind — what type of interpolation between shots.
// =========================================================================
enum class CameraTransitionKind : std::uint8_t {
    Cut          = 0,  // instant switch (no interpolation)
    SmoothBlend  = 1,  // lerp position + slerp rotation over transition frames
    Push         = 2,  // push camera directionally during transition
    WhipPan      = 3,  // fast pan/swish between shots
    FocusHandoff = 4,  // rack focus from one shot's focus to the next
};

// =========================================================================
// CameraShot — a named shot with a time range.
// =========================================================================
struct CameraShot {
    std::string              name;
    int                      start_frame{0};
    int                      end_frame{30};
    CameraProgram            program;  // the camera program for this shot
    CameraTransitionKind     transition_out{CameraTransitionKind::Cut};
    int                      transition_frames{0};  // overlap frames for transition
};

// =========================================================================
// ShotTimeline validation.
// =========================================================================
struct ShotTimelineValidationResult {
    bool ok{true};
    std::vector<std::string> errors;
};

// =========================================================================
// CameraTransition — abstract interpolation function.
// Contract: transition(0) == from_cam, transition(1) == to_cam.
// =========================================================================
class CameraTransition {
public:
    virtual ~CameraTransition() = default;
    virtual std::string id() const = 0;

    /// Interpolate from `from_cam` to `to_cam` at normalized t ∈ [0,1].
    virtual Camera2_5D evaluate(float t,
                                 const Camera2_5D& from_cam,
                                 const Camera2_5D& to_cam) const = 0;
};

// =========================================================================
// ShotTimeline — ordered sequence of shots.
// =========================================================================
class ShotTimeline {
public:
    /// Add a shot. Returns false if the shot fails validation.
    bool add_shot(CameraShot shot);

    std::size_t   size() const { return shots_.size(); }
    bool          empty() const { return shots_.empty(); }

    /// Validate the full timeline. Returns errors for: zero/negative duration,
    /// undeclared overlap, gap, out-of-order shots, transition longer than shot.
    ShotTimelineValidationResult validate() const;

    /// Find the shot active at `frame`. Returns nullptr if none.
    const CameraShot* find_shot(int frame) const;

    /// Find the shot index and the next shot (for transition overlap).
    /// Returns {-1, nullptr, nullptr} if no shot covers `frame`.
    struct ShotPair { int idx{-1}; const CameraShot* current{nullptr}; const CameraShot* next{nullptr}; };
    ShotPair find_pair(int frame) const;

    const std::vector<CameraShot>& shots() const { return shots_; }
    int total_frames() const;

private:
    std::vector<CameraShot> shots_;
};

// =========================================================================
// ShotTimelineSession — per-shot persistent constraint state.
// Avoids resetting stateful constraints (DampedFollow, banking) each frame
// during a transition overlap.
// =========================================================================
struct ShotTimelineSession {
    // Reuse the same sessions across frames keyed by shot index.
    std::unordered_map<int, CameraSession> shot_sessions;
    CameraSession& session_for(int shot_idx) { return shot_sessions[shot_idx]; }
    void reset() { shot_sessions.clear(); }
};

// =========================================================================
// ShotTimelineResolver — evaluates camera at a frame with transitions.
// =========================================================================
class ShotTimelineResolver {
public:
    explicit ShotTimelineResolver(std::shared_ptr<ShotTimeline> timeline,
                                   const class CameraTransitionCatalog* catalog = nullptr);

    /// Evaluate the camera at `frame` using the timeline + transitions.
    /// Uses local frame time (frame - shot.start_frame) for each shot's program.
    /// CAM-05 / TICKET-A3-CTX-FRAMERATE: `fps` is REQUIRED (no default
    /// fallback to 30 fps).  The CameraEvalContext::at() factory contract
    /// propagates the caller-supplied FrameRate bit-exactly into
    /// SampleTime arithmetic; this evaluate() forwards the same contract
    /// at the timeline-evaluate boundary so DampedFollow + HandheldNoise
    /// modifiers see the project FPS the upstream pass declared.
    ///
    /// Phase 1.C (TICKET-120 Sub-commit E lineage): the return type was
    /// promoted from `Camera2_5D` to
    /// `chronon3d::Result<EvaluatedCamera, CameraEvaluationError>` so
    /// transition-evaluation failures are observable via
    /// `CameraErrorCode::TransitionEvaluationFailed` instead of being
    /// silently masked as a default `Camera2_5D{}`.  Empty timeline +
    /// out-of-range frames now return `EvaluatedCamera{Camera2_5D{}, {}}`
    /// (a valid success with all-default values) so the call site can
    /// distinguish "no shot here" from "shot evaluation failed".
    chronon3d::Result<EvaluatedCamera, CameraEvaluationError>
    evaluate(int frame,
             ShotTimelineSession& timeline_session,
             FrameRate             fps) const;

    /// Set the transition for a specific kind.
    void set_transition(CameraTransitionKind kind,
                         std::shared_ptr<CameraTransition> t);

    /// Public factories for testing / direct construction.
    static std::shared_ptr<CameraTransition> default_cut();
    static std::shared_ptr<CameraTransition> default_smooth_blend();
    static std::shared_ptr<CameraTransition> default_push();
    static std::shared_ptr<CameraTransition> default_whip_pan();
    static std::shared_ptr<CameraTransition> default_focus_handoff();

private:
    std::shared_ptr<ShotTimeline> timeline_;
    std::shared_ptr<CameraTransition> get_transition(CameraTransitionKind kind) const;

    std::map<CameraTransitionKind, std::shared_ptr<CameraTransition>> transitions_;
};

// =========================================================================
// CameraTransitionCatalog — registry of transition factories.
// =========================================================================
class CameraTransitionCatalog {
public:
    using Factory = std::function<std::shared_ptr<CameraTransition>()>;

    void register_transition(CameraTransitionKind kind, Factory f);
    std::shared_ptr<CameraTransition> create(CameraTransitionKind kind) const;
    bool has(CameraTransitionKind kind) const;

    void freeze();
    // Acquire load — paired with freeze()'s release-store. Reads are
    // treated as publishing-publish-receiving synchronisation points so
    // a reader that observes frozen_=true also observes all prior
    // factories_[k] writes (acquire/release ordering).
    bool is_frozen() const noexcept { return frozen_.load(std::memory_order_acquire); }

    void register_defaults();

private:
    // TICKET-lock-free-shared_mutex — std::shared_mutex lets concurrent
    // create() / has() readers proceed in parallel. writes via
    // register_transition still take the exclusive path. After
    // freeze()=true the read paths skip the mutex entirely (acquire-load
    // of frozen_ synchronises with freeze()'s release-store).
    mutable std::shared_mutex  mu_;
    std::map<CameraTransitionKind, Factory> factories_;
    std::atomic<bool>           frozen_{false};
};

/// Backward-compatible alias for migration.
using CameraTransitionRegistry = CameraTransitionCatalog;

} // namespace chronon3d::camera_v1
