#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_constraint.hpp
//
// CameraConstraint is the v1 contract for any constraint that modifies a
// Camera2_5D after motion has been applied. Constraints are evaluated in
// stack order (insertion order, NOT auto-reordered).
//
// Stateful constraints read ConstraintSession::states[i] (per-constraint
// slot, aligned to the constraint stack index). The session is owned by
// the caller and must be reset between unrelated renders.
// ==============================================================================
#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>

#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace chronon3d::camera_v1 {

/// Result of evaluating a constraint (immutable camera snapshot).
struct ConstraintResult {
    Camera2_5D camera;
    bool       ok{true};
    std::string reason;
};

// =========================================================================
// CameraConstraintParams — typed parameters for factory creation.
// =========================================================================

struct LookAtParams       { Vec3 target{0,0,0}; };
struct KeepHorizonParams  {};
struct DampedFollowParams { float damping{0.15f}; };
struct DistanceParams     { float min_distance{10.0f}; float max_distance{10000.0f}; };
struct RotationLimitParams{ float max_pitch_deg{90.0f}; float max_yaw_deg{180.0f}; float max_roll_deg{45.0f}; };

using CameraConstraintParams = std::variant<
    LookAtParams,
    KeepHorizonParams,
    DampedFollowParams,
    DistanceParams,
    RotationLimitParams>;

// =========================================================================
// ConstraintSession — per-constraint slots for stateful constraints.
// =========================================================================

/// Per-constraint slot: state that survives across frames for one constraint.
struct ConstraintState {
    Camera2_5D previous_camera{};
    Vec3       previous_velocity{0,0,0};
    SampleTime previous_time{};
    bool       has_previous{false};
};

/// Session passed to every constraint evaluate() call. Contains one slot
/// per constraint (aligned to the stack index). The caller calls
/// ensure_states(n) before evaluating an n-constraint stack, and sets
/// active_index = i before calling constraint[i].evaluate().
struct ConstraintSession {
    std::vector<ConstraintState> states;
    std::size_t active_index{0}; ///< Set by the caller before each evaluate().
    float banking_roll{0.0f};    // shared banking state (CameraProgram)

    void ensure_states(std::size_t n) {
        if (states.size() < n) states.resize(n);
    }

    void reset() {
        for (auto& s : states) s = {};
        active_index = 0;
        banking_roll = 0.0f;
    }
};

// =========================================================================
// CameraConstraint interface
// =========================================================================

class CameraConstraint {
public:
    virtual ~CameraConstraint() = default;
    virtual std::string id() const = 0;
    virtual ConstraintResult evaluate(const Camera2_5D& in,
                                      const CameraMotionContext& ctx,
                                      ConstraintSession& session) const = 0;
};

// =========================================================================
// Factory: creates a constraint from typed parameters.
// =========================================================================

using ConstraintFactory = std::function<
    std::shared_ptr<CameraConstraint>(const CameraConstraintParams&)>;

} // namespace chronon3d::camera_v1
