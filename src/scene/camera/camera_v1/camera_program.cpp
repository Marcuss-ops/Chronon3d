// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program.cpp
//
// CameraProgram implementation — V1 compiled evaluation path.
//
// The old builder API (motion(), trajectory(), orient(), banking()) and its
// evaluate(CameraMotionContext, ConstraintSession) overload were removed in
// PR12. Only the compiled evaluate(CameraEvalContext, CameraSession) remains.
// Use compile_camera() + evaluate() for all camera evaluation.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>  // ResolvedSceneTransforms::world_position
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // quat_look_along, quat_to_camera_euler

#include <cmath>
#include <algorithm>

namespace chronon3d::camera_v1 {

// =============================================================================
// Free helpers (implementation detail, not in header).
// =============================================================================

/// Check if a CameraSourceSpec variant is time-dependent.
static bool source_is_time_dependent(const CameraSourceSpec& source) {
    if (std::holds_alternative<StaticCameraSource>(source)) {
        return false;
    }
    if (auto* pts = std::get_if<PoseTracksSource>(&source)) {
        return pts->position.is_time_dependent() ||
               pts->rotation.is_time_dependent() ||
               pts->target.is_time_dependent() ||
               pts->zoom.is_time_dependent() ||
               pts->fov_deg.is_time_dependent() ||
               pts->focus_distance.is_time_dependent() ||
               pts->aperture.is_time_dependent() ||
               pts->max_blur.is_time_dependent();
    }
    if (auto* orbit = std::get_if<OrbitMotion>(&source)) {
        return orbit->yaw.is_time_dependent() ||
               orbit->pitch.is_time_dependent() ||
               orbit->radius.is_time_dependent() ||
               orbit->track.is_time_dependent() ||
               orbit->dolly.is_time_dependent() ||
               orbit->roll.is_time_dependent() ||
               orbit->target.is_time_dependent();
    }
    if (auto* traj = std::get_if<TrajectoryMotion>(&source)) {
        return traj->trajectory != nullptr;
    }
    return true;
}

// =============================================================================
// COMPILED EVALUATION PATH
// =============================================================================

// ── Free helpers for compiled source dispatch (no header declarations needed) ──

/// Apply orientation from an OrientationSpec variant (free function).
static void apply_orientation_spec_free(const void* orient_variant,
                                         const CameraEvalContext& ctx,
                                         Camera2_5D& cam) {
    const auto& orient = *static_cast<const OrientationSpec*>(orient_variant);

    if (std::holds_alternative<FixedOrientation>(orient)) {
        return;
    }
    if (auto* lap = std::get_if<LookAtPoint>(&orient)) {
        Vec3 look_dir = lap->target - cam.position;
        float len = glm::length(look_dir);
        if (len > 1e-4f) {
            look_dir = look_dir / len;
            const Quat orientation = quat_look_along(look_dir);
            cam.rotation = quat_to_camera_euler(orientation, cam.rotation.z);
            cam.point_of_interest = lap->target;
            cam.point_of_interest_enabled = true;
        }
        return;
    }
    if (auto* lal = std::get_if<LookAtLayer>(&orient)) {
        if (ctx.transforms) {
            auto world_pos = ctx.transforms->world_position(lal->target);
            if (world_pos) {
                Vec3 look_dir = *world_pos - cam.position;
                float len = glm::length(look_dir);
                if (len > 1e-4f) {
                    look_dir = look_dir / len;
                    const Quat orientation = quat_look_along(look_dir);
                    cam.rotation = quat_to_camera_euler(orientation, cam.rotation.z);
                    cam.point_of_interest = *world_pos;
                    cam.point_of_interest_enabled = true;
                }
                return;
            }
        }
        return;
    }
    if (std::holds_alternative<OrientAlongPath>(orient)) {
        return;
    }
}

/// Evaluate a PoseTracksSource by sampling all animated channels.
static Camera2_5D eval_pose_tracks(const CameraBaseSpec& base,
                                    const OrientationSpec& orient,
                                    const PoseTracksSource& src,
                                    const CameraEvalContext& ctx) {
    Camera2_5D cam;
    cam.enabled = base.enabled;
    cam.is_animated = src.position.is_time_dependent() ||
                      src.rotation.is_time_dependent() ||
                      src.target.is_time_dependent() ||
                      src.zoom.is_time_dependent() ||
                      src.fov_deg.is_time_dependent() ||
                      src.focus_distance.is_time_dependent() ||
                      src.aperture.is_time_dependent() ||
                      src.max_blur.is_time_dependent();

    cam.position = src.position.evaluate(ctx.sample_time);
    cam.rotation = src.rotation.evaluate(ctx.sample_time);
    cam.point_of_interest = src.target.evaluate(ctx.sample_time);
    cam.point_of_interest_enabled = src.use_target;

    cam.zoom = src.zoom.evaluate(ctx.sample_time);
    cam.fov_deg = src.fov_deg.evaluate(ctx.sample_time);
    cam.projection_mode = Camera2_5DProjectionMode::Zoom;

    cam.lens = base.lens;
    cam.motion_blur = base.motion_blur;
    cam.parent_name = base.parent_name;

    // DOF: start from base defaults, then override channels that have keyframes.
    cam.dof = base.dof;
    if (src.focus_distance.is_time_dependent())
        cam.dof.focus_distance = src.focus_distance.evaluate(ctx.sample_time);
    if (src.aperture.is_time_dependent())
        cam.dof.aperture = src.aperture.evaluate(ctx.sample_time);
    if (src.max_blur.is_time_dependent())
        cam.dof.max_blur = src.max_blur.evaluate(ctx.sample_time);

    apply_orientation_spec_free(&orient, ctx, cam);

    return cam;
}

/// Evaluate an OrbitMotion by computing orbit + track + dolly.
static Camera2_5D eval_orbit_motion(const CameraBaseSpec& base,
                                     const OrientationSpec& orient,
                                     const OrbitMotion& orbit,
                                     const CameraEvalContext& ctx,
                                     bool is_animated) {
    const Vec3 target_pos = orbit.target.evaluate(ctx.sample_time);
    const float yaw_rad   = glm::radians(orbit.yaw.evaluate(ctx.sample_time));
    const float pitch_rad = glm::radians(orbit.pitch.evaluate(ctx.sample_time));
    const float radius    = orbit.radius.evaluate(ctx.sample_time);
    const Vec3  track     = orbit.track.evaluate(ctx.sample_time);
    const float dolly     = orbit.dolly.evaluate(ctx.sample_time);
    const float roll_deg  = orbit.roll.evaluate(ctx.sample_time);

    const Vec3 forward{
        std::cos(pitch_rad) * std::sin(yaw_rad),
        std::sin(pitch_rad),
        std::cos(pitch_rad) * std::cos(yaw_rad)
    };
    Vec3 pos = target_pos + forward * radius + track;
    pos.z += dolly;

    Camera2_5D cam;
    cam.enabled = base.enabled;
    cam.is_animated = is_animated;
    cam.position = pos;
    cam.rotation = Vec3{0.0f, 0.0f, roll_deg};
    cam.point_of_interest = target_pos;
    cam.point_of_interest_enabled = true;

    cam.lens = base.lens;
    cam.dof = base.dof;
    cam.motion_blur = base.motion_blur;
    cam.parent_name = base.parent_name;

    if (auto* zp = std::get_if<ZoomProjection>(&base.projection)) {
        cam.zoom = zp->zoom.evaluate(ctx.sample_time);
        cam.projection_mode = Camera2_5DProjectionMode::Zoom;
    } else if (auto* fp = std::get_if<FovProjection>(&base.projection)) {
        cam.fov_deg = fp->fov_deg.evaluate(ctx.sample_time);
        cam.projection_mode = Camera2_5DProjectionMode::Fov;
    }

    apply_orientation_spec_free(&orient, ctx, cam);
    return cam;
}

// =========================================================================
// evaluate_compiled_source — dispatch to the right evaluator by variant.
// =========================================================================

Camera2_5D CameraProgram::evaluate_compiled_source(const CameraEvalContext& ctx) const {
    const auto& source = descriptor_.source;
    const auto& base = descriptor_.base;

    if (auto* pts = std::get_if<PoseTracksSource>(&source)) {
        return eval_pose_tracks(base, descriptor_.orientation, *pts, ctx);
    }
    if (auto* orbit = std::get_if<OrbitMotion>(&source)) {
        bool animated = source_is_time_dependent(source);
        return eval_orbit_motion(base, descriptor_.orientation, *orbit, ctx, animated);
    }
    if (auto* traj = std::get_if<TrajectoryMotion>(&source)) {
        if (traj->trajectory) {
            CameraMotionContext motion_ctx;
            motion_ctx.frame = ctx.frame;
            motion_ctx.sample_time = ctx.sample_time;
            motion_ctx.base_position = base.position;
            motion_ctx.base_target = base.point_of_interest;
            motion_ctx.base_zoom = 1000.0f;
            motion_ctx.base_fov_deg = 50.0f;
            motion_ctx.base_focus_distance = base.dof.focus_distance;

            auto t = traj->trajectory->sample(motion_ctx);
            Camera2_5D cam;
            cam.enabled = base.enabled;
            cam.position = t.position;
            cam.zoom = 1000.0f;
            cam.fov_deg = 50.0f;
            cam.point_of_interest = base.point_of_interest;
            if (t.target) cam.point_of_interest = *t.target;
            cam.point_of_interest_enabled = true;

            apply_orientation_spec_free(&descriptor_.orientation, ctx, cam);
            return cam;
        }
    }

    // StaticCameraSource or unknown: use base.
    Camera2_5D cam;
    cam.enabled = base.enabled;
    cam.position = base.position;
    cam.rotation = base.rotation;
    cam.lens = base.lens;
    cam.dof = base.dof;
    cam.motion_blur = base.motion_blur;
    cam.parent_name = base.parent_name;
    cam.point_of_interest = base.point_of_interest;
    cam.point_of_interest_enabled = base.point_of_interest_enabled;

    if (auto* zp = std::get_if<ZoomProjection>(&base.projection)) {
        cam.zoom = zp->zoom.evaluate(ctx.sample_time);
        cam.projection_mode = Camera2_5DProjectionMode::Zoom;
    } else if (auto* fp = std::get_if<FovProjection>(&base.projection)) {
        cam.fov_deg = fp->fov_deg.evaluate(ctx.sample_time);
        cam.projection_mode = Camera2_5DProjectionMode::Fov;
    }

    return cam;
}

// ── Compiled constraint evaluation helper ─────────────────────────────────

/// Result from applying a single CameraConstraintSpec.
struct CompiledConstraintResult {
    Camera2_5D camera;
    bool       ok{true};
    std::string reason;
};

/// Apply a single CameraConstraintSpec to a camera.
/// @param session  Mutable session for stateful constraints (DampedFollow).
/// @param constraint_idx  Index of the constraint in the descriptor (for state slot).
static CompiledConstraintResult apply_constraint_spec(
    const CameraConstraintSpec& spec,
    const Camera2_5D& in,
    const CameraEvalContext& ctx,
    CameraSession& session,
    std::size_t constraint_idx) {

    if (auto* look_at = std::get_if<LookAtConstraint>(&spec)) {
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

        // First evaluation: initialise state and pass through unchanged.
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

        // EMA anchor: where the camera *would* be if inertia carried it forward.
        Vec3 anchor_prev = state.previous_camera.position + state.previous_velocity * dt;

        Camera2_5D cam = in;
        float a = std::clamp(damped->damping, 0.0f, 1.0f);
        cam.position = {
            in.position.x + a * (anchor_prev.x - in.position.x),
            in.position.y + a * (anchor_prev.y - in.position.y),
            in.position.z + a * (anchor_prev.z - in.position.z),
        };

        // Update state for next frame.
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

// ── apply_orientation_spec member delegates to free function ────────────────

void CameraProgram::apply_orientation_spec(const void* orient_variant,
                                            const CameraEvalContext& ctx,
                                            Camera2_5D& cam) const {
    apply_orientation_spec_free(orient_variant, ctx, cam);
}

// =========================================================================
// compiled evaluate() — no registry lookup, no mutex.
// =========================================================================

CameraProgramResult CameraProgram::evaluate(const CameraEvalContext& ctx,
                                             CameraSession& session) const {
    CameraProgramResult result;

    if (!compiled_) {
        result.diagnostics.push_back({
            CameraProgramDiagnostic::Severity::Error,
            "CameraProgram not compiled — call compile_camera() first"
        });
        result.ok = false;
        return result;
    }

    // Evaluate source directly (no registry lookup).
    Camera2_5D intermediate = evaluate_compiled_source(ctx);

    // Apply modifiers (PR1: idle oscillation only).
    for (const auto& mod : descriptor_.modifiers) {
        if (auto* idle = std::get_if<IdleOscillation>(&mod)) {
            const double t_sec = ctx.sample_time.seconds();
            const float phase = idle->frequency_hz * static_cast<float>(t_sec) * 2.0f * glm::pi<float>()
                                + idle->phase;
            const float s = std::sin(phase);
            const float c = std::cos(phase);
            intermediate.position.x += idle->position_amplitude.x * s;
            intermediate.position.y += idle->position_amplitude.y * c;
            intermediate.position.z += idle->position_amplitude.z * s;
            intermediate.rotation.x += idle->rotation_amplitude_deg.x * c;
            intermediate.rotation.y += idle->rotation_amplitude_deg.y * s;
            intermediate.rotation.z += idle->rotation_amplitude_deg.z * c;
            intermediate.zoom += idle->zoom_amplitude * s;
        }
    }

    // Re-apply orientation after modifiers.
    apply_orientation_spec(&descriptor_.orientation, ctx, intermediate);

    // Set is_animated flag.
    intermediate.is_animated = time_dependent_;

    // Carry forward lens and motion blur from descriptor base.
    // DOF is handled by the source evaluator (eval_pose_tracks applies
    // animated DOF channels; OrbitMotion and TrajectoryMotion copy base
    // DOF). Do NOT overwrite with descriptor_.base.dof here — that would
    // erase animated DOF channels from PoseTracksSource.
    intermediate.lens = descriptor_.base.lens;
    intermediate.motion_blur = descriptor_.base.motion_blur;

    // Evaluate descriptor constraints (PR6+PR10: all 5 constraint types).
    // Pre-allocate state slots for stateful constraints (DampedFollow) in session.
    session.ensure_constraint_states(descriptor_.constraints.size());

    for (std::size_t i = 0; i < descriptor_.constraints.size(); ++i) {
        session.constraint_session.active_index = i;
        auto cr = apply_constraint_spec(descriptor_.constraints[i], intermediate, ctx, session, i);
        if (!cr.ok) {
            result.diagnostics.push_back({
                CameraProgramDiagnostic::Severity::Warning,
                cr.reason
            });
            switch (failure_policy_) {
            case CameraFailurePolicy::Stop:
            case CameraFailurePolicy::KeepLastValidCamera:
                result.camera = cr.camera;
                result.ok = false;
                return result;
            case CameraFailurePolicy::SkipFailedConstraint:
                continue;
            }
        }
        intermediate = cr.camera;
    }

    result.camera = intermediate;
    result.ok = true;
    return result;
}

} // namespace chronon3d::camera_v1
