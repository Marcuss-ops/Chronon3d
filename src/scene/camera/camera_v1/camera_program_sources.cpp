// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program_sources.cpp
//
// FASE 4 Step 1 — Source evaluation helpers extracted from camera_program.cpp.
// ==============================================================================
#include "camera_program_sources.hpp"

#include <chronon3d/scene/camera/camera_v1/evaluated_projection.hpp>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>
#include <chronon3d/animation/effects/wiggle.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>

#include <glm/glm.hpp>
#include <cmath>
#include <variant>

namespace chronon3d::camera_v1 {

// =============================================================================
// TICKET-CAM-QUAT-PRIMARY — look_ahead_tangent
// =============================================================================
//
// Sample the trajectory at t + Δ and return the tangent at that future
// sample, if valid.  Used by `apply_orientation_spec_free` to anticipate
// a degenerate tangent at the current frame and substitute a valid
// look-ahead direction.  Returns `used = false` (and tangent is left
// default) if the source is not a TrajectoryMotion OR if the look-ahead
// sample is itself degenerate — the caller falls through to the next
// step in the fallback chain (last_tangent → POI → keep-base).
//
// delta_seconds defaults to 50 ms (≈ 3 frames at 60 fps) — the look-ahead
// window is intentionally short so the substitute direction tracks the
// actual scene motion rather than drifting toward a distant future.
LookAheadResult look_ahead_tangent(const CameraSourceSpec& source,
                                   const CameraEvalContext& ctx,
                                   float delta_seconds) {
    LookAheadResult out;
    auto* traj = std::get_if<TrajectoryMotion>(&source);
    if (!traj || !traj->trajectory) return out;

    // Synthesise a sub-frame SampleTime at ctx.sample_time + delta_seconds.
    const double t_now = ctx.sample_time.seconds();
    const double t_la  = t_now + static_cast<double>(delta_seconds);
    const SampleTime la_st = SampleTime::from_seconds(t_la, ctx.sample_time.frame_rate());

    // Build a synthetic CameraMotionContext for the trajectory sampler.
    // (We don't have the base here — but `trajectory->sample(ctx)` reads
    // frame + sample_time, and the base position / target are only used
    // for `use_arc_length` initialisation which is already done at
    // trajectory construction.  Sampling at a future sample_time is safe
    // even with default base_* fields.)
    CameraMotionContext motion_ctx;
    motion_ctx.frame = ctx.frame;
    motion_ctx.sample_time = la_st;

    auto s = traj->trajectory->sample(motion_ctx);
    if (glm::length(s.tangent) > 1e-6f) {
        out.tangent = glm::normalize(s.tangent);
        out.used = true;
    }
    return out;
}

namespace chronon3d::camera_v1 {

// =============================================================================
// source_is_time_dependent
// =============================================================================

bool source_is_time_dependent(const CameraSourceSpec& source) {
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
// apply_projection_spec — CAM-03 / DOC 02 central dispatch
// =============================================================================

void apply_projection_spec(const ProjectionSpec& spec,
                           const CameraEvalContext& ctx,
                           Camera2_5D& cam) {
    std::visit([&](auto&& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, ZoomProjection>) {
            cam.zoom = p.zoom.evaluate(ctx.sample_time);
            cam.optics_mode = CameraOpticsMode::Zoom;
        } else if constexpr (std::is_same_v<T, FovProjection>) {
            cam.fov_deg = p.fov_deg.evaluate(ctx.sample_time);
            cam.optics_mode = CameraOpticsMode::FieldOfView;
        } else if constexpr (std::is_same_v<T, PhysicalLensProjection>) {
            cam.lens = p.lens;
            cam.optics_mode = CameraOpticsMode::PhysicalLens;
            cam.zoom = 0.0f;
            cam.fov_deg = 0.0f;
        }
    }, spec);
}

// =============================================================================
// eval_pose_tracks
// =============================================================================

Camera2_5D eval_pose_tracks(const CameraBaseSpec& base,
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

    apply_projection_spec(base.projection, ctx, cam);
    std::visit([&](auto&& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, ZoomProjection>) {
            if (src.zoom.is_time_dependent())
                cam.zoom = src.zoom.evaluate(ctx.sample_time);
        } else if constexpr (std::is_same_v<T, FovProjection>) {
            if (src.fov_deg.is_time_dependent())
                cam.fov_deg = src.fov_deg.evaluate(ctx.sample_time);
        } else if constexpr (std::is_same_v<T, PhysicalLensProjection>) {
            (void)p;
        } else {
            static_assert(std::is_void_v<T>,
                "PoseTracksSource: unhandled ProjectionSpec variant — "
                "update eval_pose_tracks std::visit AND "
                "apply_projection_spec together.");
        }
    }, base.projection);

    if (!std::holds_alternative<PhysicalLensProjection>(base.projection)) {
        cam.lens = base.lens;
    }
    cam.motion_blur = base.motion_blur;
    cam.parent_name = base.parent_name;

    cam.dof = base.dof;
    if (src.focus_distance.is_time_dependent())
        cam.dof.focus_distance = src.focus_distance.evaluate(ctx.sample_time);
    if (src.aperture.is_time_dependent())
        cam.dof.aperture = src.aperture.evaluate(ctx.sample_time);
    if (src.max_blur.is_time_dependent())
        cam.dof.max_blur = src.max_blur.evaluate(ctx.sample_time);

    return cam;
}

// =============================================================================
// eval_orbit_motion — TICKET-024 fix
// =============================================================================

Camera2_5D eval_orbit_motion(const CameraBaseSpec& base,
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

    const Vec3 world_forward{
        std::cos(pitch_rad) * std::sin(yaw_rad),
        std::sin(pitch_rad),
        std::cos(pitch_rad) * std::cos(yaw_rad)
    };
    const Vec3 orbit_position = target_pos + world_forward * radius;

    Vec3 basis_forward = target_pos - orbit_position;
    float basis_fwd_len = glm::length(basis_forward);
    if (basis_fwd_len > 1e-4f) {
        basis_forward /= basis_fwd_len;
    } else {
        basis_forward = Vec3{
            -world_forward.x,
            -world_forward.y,
            -world_forward.z
        };
    }

    const Vec3 world_up{0.0f, 1.0f, 0.0f};
    Vec3 basis_right = glm::cross(basis_forward, world_up);
    float basis_right_len = glm::length(basis_right);
    if (basis_right_len > 1e-4f) {
        basis_right /= basis_right_len;
    } else {
        basis_right = Vec3{1.0f, 0.0f, 0.0f};
    }
    Vec3 basis_up = glm::cross(basis_right, basis_forward);

    Vec3 pos = orbit_position
             + track.x * basis_right
             + track.y * basis_up
             + dolly * basis_forward;

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

    apply_projection_spec(base.projection, ctx, cam);

    return cam;
}

} // namespace chronon3d::camera_v1
