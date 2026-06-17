// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program.cpp
//
// CameraProgram implementation — V1 contract completion.
//
// Adds:
//   - CameraProgramSource variant (no ambiguous motion+trajectory states)
//   - CameraProgramResult + CameraProgramDiagnostic + CameraFailurePolicy
//   - OrientationPolicy fully plumbed (5 modes)
//   - Banking with curvature-derived roll + EMA smoothing in ConstraintSession
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // quat_look_along, quat_to_camera_euler

#include <cmath>
#include <algorithm>

namespace chronon3d::camera_v1 {

// =========================================================================
// Source management
// =========================================================================

CameraProgram& CameraProgram::motion(const std::string& id) {
    bool found = CameraMotionRegistry::instance().has(id);
    source_ = RegisteredMotionSource{id, found};
    return *this;
}

CameraProgram& CameraProgram::trajectory(std::shared_ptr<CameraTrajectory> t) {
    source_ = TrajectorySource{std::move(t)};
    return *this;
}

CameraProgram& CameraProgram::add_constraint(std::shared_ptr<CameraConstraint> c) {
    constraints_.add(std::move(c));
    return *this;
}

CameraProgram& CameraProgram::add_constraint_named(const std::string& registry_id) {
    auto c = CameraConstraintRegistry::instance().create(registry_id);
    if (c) constraints_.add(std::move(c));
    return *this;
}

bool CameraProgram::has_motion() const {
    auto* m = std::get_if<RegisteredMotionSource>(&source_);
    return m && m->valid;
}

bool CameraProgram::has_trajectory() const {
    return std::holds_alternative<TrajectorySource>(source_);
}

// =========================================================================
// Orientation
// =========================================================================

void CameraProgram::apply_orientation(const CameraTrajectorySample& s,
                                       const CameraMotionContext& ctx,
                                       Camera2_5D& cam) const {
    switch (orient_) {
    case OrientationPolicy::FixedRotation:
        break;  // keep existing rotation

    case OrientationPolicy::LookAtPoint: {
        Vec3 look_dir = ctx.base_target - cam.position;
        float len = glm::length(look_dir);
        if (len > 1e-4f) {
            look_dir = look_dir / len;
            const Quat orientation = quat_look_along(look_dir);
            cam.rotation = quat_to_camera_euler(orientation, cam.rotation.z);
            cam.point_of_interest = ctx.base_target;
            cam.point_of_interest_enabled = true;
        }
        break;
    }

    case OrientationPolicy::LookAtLayer:
        // Layers not plumbed at this level — fall through to LookAtPoint.
        // P6 (CameraFramingSolver) will add per-layer POI extraction.
        if (ctx.base_target.x != 0.0f || ctx.base_target.y != 0.0f || ctx.base_target.z != 0.0f) {
            Vec3 look_dir = ctx.base_target - cam.position;
            float len = glm::length(look_dir);
            if (len > 1e-4f) {
                look_dir = look_dir / len;
                const Quat orientation = quat_look_along(look_dir);
                cam.rotation = quat_to_camera_euler(orientation, cam.rotation.z);
                cam.point_of_interest = ctx.base_target;
                cam.point_of_interest_enabled = true;
            }
        }
        break;

    case OrientationPolicy::OrientAlongPath: {
        Vec3 forward = s.tangent;
        float len = glm::length(forward);
        if (len > 1e-4f) {
            forward = forward / len;
            const Quat orientation = quat_look_along(forward);
            cam.rotation = quat_to_camera_euler(orientation, cam.rotation.z);
            // Set POI along the path direction
            cam.point_of_interest = cam.position + forward * 1000.0f;
            cam.point_of_interest_enabled = true;
        }
        break;
    }

    case OrientationPolicy::OrientAlongPathKeepHorizon: {
        Vec3 forward = s.tangent;
        float len = glm::length(forward);
        if (len > 1e-4f) {
            forward = forward / len;
            const Quat orientation = quat_look_along(forward);
            cam.rotation = quat_to_camera_euler(orientation, 0.0f);  // zero roll
            cam.point_of_interest = cam.position + forward * 1000.0f;
            cam.point_of_interest_enabled = true;
        }
        break;
    }

    case OrientationPolicy::Custom:
        // Reserved for P7 callback-based orientation. No-op for now.
        break;
    }
}

// =========================================================================
// Banking — curvature-derived roll with EMA smoothing in session.
// =========================================================================

float CameraProgram::apply_banking(float roll_deg,
                                    const CameraMotionContext& ctx,
                                    ConstraintSession& session) const {
    if (!banking_.enabled) return roll_deg;

    // Use the session to store our per-constraint banking state.
    // We piggy-back on previous_velocity as a banking_roll accumulator.
    // (P5 will add per-constraint state slots to ConstraintSession; for V1
    // we reuse the existing fields to avoid API churn.)
    float prev_roll = session.banking_roll;

    // Curvature-derived target roll: use the trajectory tangent change.
    // tangents come from the trajectory sample; we defer the curvature calc
    // to the caller (sample_trajectory_cam) which passes the raw roll_deg.
    // Here we smooth: target_roll = curvature_based_roll (passed in as roll_deg
    // after banking calc in sample_trajectory_cam).
    float a = std::clamp(banking_.smoothing, 0.0f, 1.0f);
    float smoothed = prev_roll + a * (roll_deg - prev_roll);

    // Hard clamp.
    float max_r = std::max(0.0f, banking_.max_roll_deg);
    if (smoothed >  max_r) smoothed =  max_r;
    if (smoothed < -max_r) smoothed = -max_r;

    session.banking_roll = smoothed;
    return smoothed;
}

// =========================================================================
// Trajectory sampling + orientation + banking
// =========================================================================

Camera2_5D CameraProgram::sample_trajectory_cam(const CameraMotionContext& ctx,
                                                 Vec3 fallback_target,
                                                 ConstraintSession& session,
                                                 CameraProgramResult& result) const {
    const auto* traj_src = std::get_if<TrajectorySource>(&source_);
    if (!traj_src || !traj_src->trajectory) {
        result.diagnostics.push_back({
            CameraProgramDiagnostic::Severity::Error,
            "sample_trajectory_cam called without trajectory source"
        });
        result.ok = false;
        return base_;
    }

    auto s = traj_src->trajectory->sample(ctx);
    Camera2_5D cam = base_;
    cam.position = s.position;

    if (s.target) cam.point_of_interest = *s.target;
    else          cam.point_of_interest = fallback_target;
    cam.point_of_interest_enabled = true;

    // Apply orientation policy.
    apply_orientation(s, ctx, cam);

    // Apply banking: curvature from trajectory tangent change.
    float roll = s.roll_deg;
    if (banking_.enabled && has_trajectory()) {
        // Compute curvature from tangents at t-1 and t+1.
        auto ctx_prev = ctx;
        ctx_prev.sample_time = SampleTime::from_frame(
            ctx.sample_time.frame - 1.0, ctx.sample_time.frame_rate);
        auto ctx_next = ctx;
        ctx_next.sample_time = SampleTime::from_frame(
            ctx.sample_time.frame + 1.0, ctx.sample_time.frame_rate);

        auto s_prev = traj_src->trajectory->sample(ctx_prev);
        auto s_next = traj_src->trajectory->sample(ctx_next);

        Vec3 tangent_prev = s_prev.tangent;
        Vec3 tangent_next = s_next.tangent;
        Vec3 tangent_curr = s.tangent;

        float curvature = glm::length(tangent_next - tangent_prev);
        const Vec3 world_up{0.0f, 1.0f, 0.0f};
        float signed_turn = glm::dot(
            glm::cross(tangent_prev, tangent_next), world_up);
        roll = std::clamp(signed_turn * banking_.strength,
                          -banking_.max_roll_deg,
                           banking_.max_roll_deg);
    }
    roll = apply_banking(roll, ctx, session);
    cam.set_roll(roll);

    return cam;
}

// =========================================================================
// evaluate() — structured result, diagnostics, failure policy.
// =========================================================================

CameraProgramResult CameraProgram::evaluate(const CameraMotionContext& ctx,
                                             ConstraintSession& session) const {
    CameraProgramResult result;

    Camera2_5D intermediate = base_;

    if (has_trajectory()) {
        intermediate = sample_trajectory_cam(ctx, ctx.base_target, session, result);
    } else if (has_motion()) {
        auto& motion_src = std::get<RegisteredMotionSource>(source_);
        intermediate = CameraMotionRegistry::instance().build(
            motion_src.id, ctx, base_);
        if (intermediate.position.x == base_.position.x
         && intermediate.position.y == base_.position.y
         && intermediate.position.z == base_.position.z
         && motion_src.id != "") {
            // build() returns base_ unchanged when the motion id is missing.
            result.diagnostics.push_back({
                CameraProgramDiagnostic::Severity::Warning,
                "motion '" + motion_src.id + "' not found — using base camera"
            });
        }
    }

    if (constraints_.empty()) {
        result.camera = intermediate;
        result.ok = true;
        return result;
    }

    // Run constraint stack with failure policy.
    ConstraintResult r;
    r.camera = intermediate;
    r.ok = true;

    session.ensure_states(constraints_.size());

    for (auto& c : constraints_.all()) {
        auto cr = c->evaluate(r.camera, ctx, session);

        if (!cr.ok) {
            result.diagnostics.push_back({
                CameraProgramDiagnostic::Severity::Warning,
                "constraint '" + c->id() + "' failed: " + cr.reason
            });

            switch (failure_policy_) {
            case CameraFailurePolicy::Stop:
                result.camera = cr.camera;
                result.ok = false;
                return result;

            case CameraFailurePolicy::SkipFailedConstraint:
                // Skip this constraint, keep r.camera unchanged.
                // NOTE: P5 — the failed constraint may have mutated session
                // state (previous_camera, previous_velocity). Per-constraint
                // state slots in ConstraintSession will isolate these.
                continue;

            case CameraFailurePolicy::KeepLastValidCamera:
                result.camera = r.camera;
                result.ok = false;
                return result;
            }
        }

        r = cr;  // advance
    }

    result.camera = r.camera;
    result.ok = true;
    return result;
}

} // namespace chronon3d::camera_v1
