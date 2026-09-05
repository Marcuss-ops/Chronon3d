// ═══════════════════════════════════════════════════════════════════════════
// camera_program_source_dispatch.cpp — evaluate_compiled_source — dispatch to the right evaluator by variant
//
// Split out of camera_program.cpp (pure code move, no behavior change).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/evaluated_projection.hpp>  // CAM-03 snapshot
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>  // ResolvedSceneTransforms::world_position
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // quat_look_along, quat_to_camera_euler
#include <chronon3d/animation/effects/wiggle.hpp>             // CAM-04 — wiggle3D abs-time

#include "camera_program_sources.hpp"  // FASE 4 Step 1 — source eval helpers
#include "camera_program_constraints.hpp"  // FASE 4 Step 2 — constraint eval

#include <cmath>
#include <algorithm>
#include <type_traits>

namespace chronon3d::camera_v1 {

// =========================================================================
// evaluate_compiled_source — dispatch to the right evaluator by variant.
// =========================================================================

EvaluatedCameraSource CameraProgram::evaluate_compiled_source(const CameraEvalContext& ctx) const {
    const auto& source = descriptor_.source;
    const auto& base = descriptor_.base;

    if (auto* pts = std::get_if<PoseTracksSource>(&source)) {
        // TICKET-022 / DOC 02 — orientation is no longer passed to source evaluators;
        // CameraProgram::evaluate() applies it once after modifiers.
        return EvaluatedCameraSource{eval_pose_tracks(base, *pts, ctx), std::nullopt, std::nullopt};
    }
    if (auto* orbit = std::get_if<OrbitMotion>(&source)) {
        bool animated = source_is_time_dependent(source);
        return EvaluatedCameraSource{eval_orbit_motion(base, *orbit, ctx, animated), std::nullopt, std::nullopt};
    }
    if (auto* traj = std::get_if<TrajectoryMotion>(&source)) {
        if (traj->trajectory) {
            CameraMotionContext motion_ctx;
            motion_ctx.frame = ctx.frame;
            motion_ctx.sample_time = ctx.sample_time;
            motion_ctx.base_position = base.position;
            motion_ctx.base_target = base.point_of_interest;

            auto t = traj->trajectory->sample(motion_ctx);

            // Start from the full CameraBaseSpec — not a stripped-down camera.
            // The trajectory overrides ONLY position and target; everything
            // else (lens, projection, DOF, motion blur, parent_name, rotation)
            // comes from the base spec.
            Camera2_5D cam;
            cam.enabled = base.enabled;
            cam.is_animated = true;

            // Base rotation (may be overridden by the orientation stage later).
            cam.rotation = base.rotation;

            // Apply the canonical projection dispatch (Zoom / Fov / PhysicalLens).
            apply_projection_spec(base.projection, ctx, cam);

            // Lens: from PhysicalLensProjection if active, else from base.
            if (!std::holds_alternative<PhysicalLensProjection>(base.projection)) {
                cam.lens = base.lens;
            }

            // Transfer DOF, motion blur, and parent_name from base.
            cam.dof = base.dof;
            cam.motion_blur = base.motion_blur;
            cam.parent_name = base.parent_name;

            // Override position and target from trajectory sample.
            cam.position = t.position;
            cam.point_of_interest = base.point_of_interest;
            if (t.target) cam.point_of_interest = *t.target;
            cam.point_of_interest_enabled = true;

            // TICKET-022 / DOC 02 — orientation is applied ONCE in evaluate() after modifiers.
            EvaluatedCameraSource result;
            result.camera = cam;
            result.tangent = (glm::length(t.tangent) > 1e-6f)
                ? std::optional<Vec3>(glm::normalize(t.tangent))
                : std::nullopt;
            result.roll_deg = t.roll_deg;
            return result;
        }
    }

    if (auto* cmps = std::get_if<CameraMotionParamsSource>(&source)) {
        // TICKET-P2-29: continuous-time evaluation.  The 60-sample discrete
        // bake (constexpr int n = 60; for (...) bake) is GONE — the V1
        // runtime evaluates the motion math natively via sample_at() at the
        // given frame.  Mathematically equivalent within ε to the prior
        // bake (61 keyframes via linear interpolation between samples).
        Camera2_5D cam = cmps->sample_at(ctx.frame);
        // Carry forward base fields (lens, DOF, motion blur, parent_name).
        cam.lens = base.lens;
        cam.dof = base.dof;
        cam.motion_blur = base.motion_blur;
        cam.parent_name = base.parent_name;
        // sample_at() already evaluated the projection (zoom) channel; do NOT
        // re-apply the static base projection or we would clobber the
        // animated zoom.  The adapter always uses ZoomProjection, so the
        // optics mode stays Zoom.
        cam.optics_mode = CameraOpticsMode::Zoom;
        // No trajectory tangent / roll — CameraMotionParamsSource is not
        // a trajectory; the orientation stage handles its own fallbacks
        // (Fixed / LookAt / etc.).
        return EvaluatedCameraSource{cam, std::nullopt, std::nullopt};
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

    // CAM-03: central projection dispatch (handles all 3 variants).
    apply_projection_spec(base.projection, ctx, cam);

    return EvaluatedCameraSource{cam, std::nullopt, std::nullopt};
}

} // namespace chronon3d::camera_v1
