// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program_constraints.cpp
//
// FASE 4 Step 2 — Constraint evaluation extracted from camera_program.cpp.
// ==============================================================================
#include "camera_program_constraints.hpp"

#include <chronon3d/animation/path/spatial_bezier_path.hpp>

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <variant>

namespace chronon3d::camera_v1 {

CompiledConstraintResult apply_constraint_spec(
    const CameraConstraintSpec& spec,
    const Camera2_5D& in,
    const CameraEvalContext& ctx,
    CameraSession& session,
    std::size_t constraint_idx) {

    if (auto* look_at = std::get_if<LookAtConstraint>(&spec)) {
        if (session.skip_look_at_constraint_from_orientation) {
            return {in, true};
        }
        Vec3 look_dir = look_at->target - in.position;
        float len = glm::length(look_dir);
        if (len > 1e-4f) {
            Camera2_5D cam = in;
            look_dir = look_dir / len;
            const Quat orientation = quat_look_along(look_dir);
            cam.rotation = quat_to_camera_euler(orientation, cam.rotation.z);
            cam.point_of_interest = look_at->target;
            cam.point_of_interest_enabled = true;
            return {cam, true};
        }
        return {in, true};
    }

    if (std::holds_alternative<KeepHorizonConstraint>(spec)) {
        Camera2_5D cam = in;
        cam.rotation.z = 0.0f;
        return {cam, true};
    }

    if (auto* damped = std::get_if<DampedFollowConstraint>(&spec)) {
        auto& state = session.constraint_session.states.at(constraint_idx);

        if (!state.has_previous) {
            state.previous_camera = in;
            state.previous_velocity = {0.0f, 0.0f, 0.0f};
            state.previous_time = ctx.sample_time;
            state.has_previous = true;
            return {in, true};
        }

        float dt = ctx.sample_time.seconds() - state.previous_time.seconds();
        if (dt <= 0.0f) {
            return {state.previous_camera, true};
        }

        Vec3 anchor_prev = state.previous_camera.position + state.previous_velocity * dt;

        Camera2_5D cam = in;
        float a = std::clamp(damped->damping, 0.0f, 1.0f);
        cam.position = {
            in.position.x + a * (anchor_prev.x - in.position.x),
            in.position.y + a * (anchor_prev.y - in.position.y),
            in.position.z + a * (anchor_prev.z - in.position.z),
        };

        state.previous_velocity = {
            (cam.position.x - state.previous_camera.position.x) / std::max(1e-6f, dt),
            (cam.position.y - state.previous_camera.position.y) / std::max(1e-6f, dt),
            (cam.position.z - state.previous_camera.position.z) / std::max(1e-6f, dt),
        };
        state.previous_camera = cam;
        state.previous_time = ctx.sample_time;
        return {cam, true};
    }

    if (auto* dist = std::get_if<DistanceConstraint>(&spec)) {
        Vec3 target = in.point_of_interest_enabled
            ? in.point_of_interest
            : Vec3{in.position.x, in.position.y, in.position.z - 1000.0f};

        Vec3 to_target = target - in.position;
        float d = glm::length(to_target);
        if (d < 1e-3f) {
            return {in, false, "distance-zero"};
        }
        Vec3 dir = to_target / d;
        float clamped = std::clamp(d, dist->min_distance, dist->max_distance);
        Camera2_5D cam = in;
        cam.position = target - dir * clamped;
        return {cam, true};
    }

    if (auto* rot = std::get_if<RotationLimitConstraint>(&spec)) {
        Camera2_5D cam = in;
        float max_pitch = std::max(0.0f, rot->max_pitch_deg);
        float max_yaw   = std::max(0.0f, rot->max_yaw_deg);
        float max_roll  = std::max(0.0f, rot->max_roll_deg);
        cam.rotation.x = std::clamp(in.rotation.x, -max_pitch, max_pitch);
        cam.rotation.y = std::clamp(in.rotation.y, -max_yaw, max_yaw);
        cam.rotation.z = std::clamp(in.rotation.z, -max_roll, max_roll);
        return {cam, true};
    }

    return {in, true};
}

} // namespace chronon3d::camera_v1
