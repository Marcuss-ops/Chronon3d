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
//   ShotTimeline         → ordered list, find active shot
//   ShotTimelineResolver → evaluate camera at frame, apply transitions
//   CameraTransitionRegistry → singleton registry of transition factories
// ==============================================================================
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

// =========================================================================
// Transition kind — what type of interpolation between shots.
// =========================================================================
enum class CameraTransitionKind : std::uint8_t {
    Cut          = 0,  // instant switch (no interpolation)
    SmoothBlend  = 1,  // lerp position + rotation (euler mix) over transition frames
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
    CameraTransitionKind     transition_in{CameraTransitionKind::Cut};
    int                      transition_frames{0};  // overlap frames for transition
};

// =========================================================================
// CameraTransition — abstract interpolation function.
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
    ShotTimeline& add_shot(CameraShot shot);
    std::size_t   size() const { return shots_.size(); }
    bool          empty() const { return shots_.empty(); }

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
// ShotTimelineResolver — evaluates camera at a frame with transitions.
// =========================================================================
class ShotTimelineResolver {
public:
    explicit ShotTimelineResolver(std::shared_ptr<ShotTimeline> timeline);

    /// Evaluate the camera at `frame` using the timeline + transitions.
    Camera2_5D evaluate(int frame, ConstraintSession& session) const;

    /// Set the transition for a specific kind.
    void set_transition(CameraTransitionKind kind,
                         std::shared_ptr<CameraTransition> t);

private:
    std::shared_ptr<ShotTimeline> timeline_;
    std::shared_ptr<CameraTransition> get_transition(CameraTransitionKind kind) const;

    static std::shared_ptr<CameraTransition> default_cut();
    static std::shared_ptr<CameraTransition> default_smooth_blend();
    static std::shared_ptr<CameraTransition> default_push();
    static std::shared_ptr<CameraTransition> default_whip_pan();
    static std::shared_ptr<CameraTransition> default_focus_handoff();

    std::map<CameraTransitionKind, std::shared_ptr<CameraTransition>> transitions_;
};

// =========================================================================
// CameraTransitionRegistry — singleton registry of transition factories.
// =========================================================================
class CameraTransitionRegistry {
public:
    static CameraTransitionRegistry& instance();

    using Factory = std::function<std::shared_ptr<CameraTransition>()>;

    void register_transition(CameraTransitionKind kind, Factory f);
    std::shared_ptr<CameraTransition> create(CameraTransitionKind kind) const;
    bool has(CameraTransitionKind kind) const;

private:
    CameraTransitionRegistry() = default;
    mutable std::mutex mu_;
    std::map<CameraTransitionKind, Factory> factories_;
};

} // namespace chronon3d::camera_v1
