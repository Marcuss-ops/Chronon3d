#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_program.hpp
//
// CameraProgram is the V1 *single evaluation entry-point*. Any composition
// that wants a camera goes through it:
//
//     scene.camera().set(
//         CameraProgram{}
//             .motion("camera.hero_push", ctx)
//             .add_constraint("camera.look_at")
//             .add_constraint("camera.keep_horizon")
//             .evaluate(base_cam, ctx));
//
// It's deliberately a thin orchestration layer over:
//   1. base cam (from scene.camera())
//   2. motion lookup   -> CameraMotionRegistry
//   3. trajectory sampling -> CameraTrajectory (optional)
//   4. constraint stack     -> CameraConstraintRegistry
//   5. validation           -> CameraShotValidator (existing)
//
// This file owns NO new logic — only the chain itself.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_constraint_resolver.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>

#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D

#include <memory>
#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

/// Optional orientation policy a program can attach to a trajectory.
enum class OrientationPolicy : std::uint8_t {
    FixedRotation = 0,
    LookAtPoint   = 1,
    LookAtLayer   = 2,
    OrientAlongPath = 3,
    OrientAlongPathKeepHorizon = 4,
    Custom        = 5,
};

/// Banking controls (degrees of roll smoothed over path).
struct BankingSettings {
    bool  enabled{false};
    float strength{0.15f};
    float max_roll_deg{8.0f};
    float smoothing{0.8f};
};

/// The V1 program. Mutable for builder-chain ergonomics; immutable once
/// captured by reference at evaluate time.
class CameraProgram {
public:
    CameraProgram() = default;

    /// Source-of-truth: the camera after motion / trajectory before constraints.
    CameraProgram& base(const Camera2_5D& cam) { base_ = cam; return *this; }
    const Camera2_5D& base_cam() const { return base_; }

    /// Motion preset id (mutually exclusive with trajectory).
    CameraProgram& motion(const std::string& id);

    /// Trajectory: if set, motion() is ignored.
    CameraProgram& trajectory(std::shared_ptr<CameraTrajectory> t);

    /// Constraints (registered in the order added).
    CameraProgram& add_constraint(std::shared_ptr<CameraConstraint> c);
    CameraProgram& add_constraint_named(const std::string& registry_id);

    /// Orientation policy applied AFTER trajectory sampling, BEFORE constraints.
    CameraProgram& orient(OrientationPolicy p) { orient_ = p; return *this; }
    CameraProgram& banking(BankingSettings b) { banking_ = b; return *this; }

    bool has_motion() const  { return motion_valid_; }
    bool has_trajectory() const { return trajectory_ != nullptr; }
    bool has_constraints() const { return !constraints_.empty(); }

    /// One-shot evaluator. Returns the final Camera2_5D.
    Camera2_5D evaluate(const CameraMotionContext& ctx,
                        ConstraintSession& session) const;

private:
    Camera2_5D                        base_{};
    std::string                       motion_id_;
    bool                              motion_valid_{false};
    std::shared_ptr<CameraTrajectory> trajectory_;
    CameraConstraintStack             constraints_;
    OrientationPolicy                 orient_{OrientationPolicy::FixedRotation};
    BankingSettings                   banking_{};

    // Sample trajectory into a Camera2_5D using orientation + banking.
    Camera2_5D sample_trajectory_cam(const CameraMotionContext& ctx,
                                     Vec3 fallback_target) const;
};

} // namespace chronon3d::camera_v1
