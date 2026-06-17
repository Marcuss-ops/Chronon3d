#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_constraint.hpp
//
// CameraConstraint is the v1 contract for any constraint that modifies a
// Camera2_5D after motion has been applied. Constraints are evaluated in
// stack order (insertion order, NOT auto-reordered) — order matters because
// damped / rail / look-at can interact.
//
// Stateful constraints (damped follow, lookahead) read `ConstraintSession`
// to access previous_camera / previous_velocity / previous_time. The session
// is owned by the caller and must be reset between unrelated renders.
// ==============================================================================
#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>

#include <string>

namespace chronon3d::camera_v1 {

/// Result of evaluating a constraint (immutable camera snapshot).
struct ConstraintResult {
    Camera2_5D camera;
    bool       ok{true};             ///< false = constraint could not be satisfied.
    std::string reason;              ///< "target-behind-camera", "rail-degenerate", ...
};

/// Per-render state object passed to stateful constraints.
struct ConstraintSession {
    Camera2_5D previous_camera{};
    Vec3       previous_velocity{0,0,0};
    SampleTime previous_time{};
    bool       has_previous{false};

    void reset() {
        previous_camera = {};
        previous_velocity = {0,0,0};
        previous_time = {};
        has_previous = false;
    }
};

/// Stateless or session-aware constraint: evaluate(in_camera, ctx, session) -> out_camera.
class CameraConstraint {
public:
    virtual ~CameraConstraint() = default;
    virtual std::string id() const = 0;
    virtual ConstraintResult evaluate(const Camera2_5D& in,
                                      const CameraMotionContext& ctx,
                                      ConstraintSession& session) const = 0;
};

} // namespace chronon3d::camera_v1
