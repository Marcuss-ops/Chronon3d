// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program.cpp
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>  // CameraConstraintRegistry

#include <cmath>
#include <stdexcept>

namespace chronon3d::camera_v1 {

CameraProgram& CameraProgram::motion(const std::string& id) {
    motion_id_ = id;
    motion_valid_ = CameraMotionRegistry::instance().has(id);
    if (!motion_valid_) {
        // Soft-warn path: leave base_ unchanged when the id is missing.
    }
    return *this;
}

CameraProgram& CameraProgram::trajectory(std::shared_ptr<CameraTrajectory> t) {
    trajectory_ = std::move(t);
    return *this;
}

CameraProgram& CameraProgram::add_constraint(std::shared_ptr<CameraConstraint> c) {
    if (c) constraints_.add(std::move(c));
    return *this;
}

CameraProgram& CameraProgram::add_constraint_named(const std::string& registry_id) {
    auto c = CameraConstraintRegistry::instance().create(registry_id);
    if (c) constraints_.add(std::move(c));
    return *this;
}

Camera2_5D CameraProgram::sample_trajectory_cam(const CameraMotionContext& ctx,
                                                Vec3 fallback_target) const {
    if (!trajectory_) return base_;
    auto s = trajectory_->sample(ctx);
    Camera2_5D cam = base_;
    cam.position = s.position;
    if (s.target) cam.point_of_interest = *s.target;
    else         cam.point_of_interest = fallback_target;
    cam.point_of_interest_enabled = true;

    float roll = s.roll_deg;
    if (banking_.enabled) {
        // Soft-clamp against max_roll_deg.
        float max_r = std::max(0.0f, banking_.max_roll_deg);
        if (roll >  max_r) roll =  max_r;
        if (roll < -max_r) roll = -max_r;
    }
    cam.set_roll(roll);
    return cam;
}

Camera2_5D CameraProgram::evaluate(const CameraMotionContext& ctx,
                                    ConstraintSession& session) const {
    Camera2_5D intermediate = base_;
    if (has_trajectory()) {
        intermediate = sample_trajectory_cam(ctx, ctx.base_target);
    } else if (has_motion()) {
        intermediate = CameraMotionRegistry::instance().build(motion_id_, ctx, base_);
    }

    if (constraints_.empty()) {
        return intermediate;
    }
    auto r = constraints_.resolve(intermediate, ctx, session);
    return r.camera;
}

} // namespace chronon3d::camera_v1
