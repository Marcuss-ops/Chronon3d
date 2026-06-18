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
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_motion_descriptor.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>
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

    for (std::size_t i = 0; i < constraints_.all().size(); ++i) {
        auto& c = constraints_.all()[i];
        session.active_index = i;
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

// =============================================================================
// COMPILED EVALUATION PATH (PR1) — no registry lookup in evaluate().
// =============================================================================

// =========================================================================
// evaluate_compiled_source — dispatch to the right evaluator by variant.
// =========================================================================

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

    // PR3: if compile_camera() resolved a RegisteredMotionRef via
    // CameraMotionRegistry fallback, call it directly instead of
    // dispatching on the descriptor source.
    if (resolved_motion_) {
        CameraMotionContext motion_ctx;
        motion_ctx.frame = ctx.frame;
        motion_ctx.sample_time = ctx.sample_time;
        motion_ctx.base_position = base.position;
        motion_ctx.base_target = base.point_of_interest;
        if (auto* zp = std::get_if<ZoomProjection>(&base.projection)) {
            motion_ctx.base_zoom = zp->zoom.evaluate(ctx.sample_time);
        } else if (auto* fp = std::get_if<FovProjection>(&base.projection)) {
            motion_ctx.base_fov_deg = fp->fov_deg.evaluate(ctx.sample_time);
        }
        motion_ctx.base_focus_distance = base.dof.focus_distance;
        return resolved_motion_->evaluate(motion_ctx);
    }

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

    // TODO(PR7): evaluate descriptor constraints.

    result.camera = intermediate;
    result.ok = true;
    return result;
}

} // namespace chronon3d::camera_v1
