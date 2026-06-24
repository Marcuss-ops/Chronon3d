// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program.cpp
//
// CameraProgram implementation — V1 compiled evaluation path.
//
// CAM-03 / DOC 02 extensions:
//   - ProjectionSpec dispatch is centralised in apply_projection_spec()
//     (handles ZoomProjection / FovProjection / PhysicalLensProjection).
//   - The EvaluatedProjection snapshot is exposed via
//     camera_math::make_evaluated_projection() defined in
//     scene/camera/camera_v1/evaluated_projection.hpp.
//   - Single look-at policy: when both an OrientationSpec look-at AND a
//     LookAtConstraint are present, the orientation wins and the constraint
//     is skipped (CAM-03 / DOC 02).  A Warning diagnostic is emitted so
//     composition authors can correct their descriptor.
//
// The old builder API (motion(), trajectory(), orient(), banking()) and its
// evaluate(CameraMotionContext, ConstraintSession) overload were removed in
// PR12. Only the compiled evaluate(CameraEvalContext, CameraSession) remains.
// Use compile_camera() + evaluate() for all camera evaluation.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/evaluated_projection.hpp>  // CAM-03 snapshot
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>  // ResolvedSceneTransforms::world_position
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // quat_look_along, quat_to_camera_euler
#include <chronon3d/animation/effects/wiggle.hpp>             // CAM-04 — wiggle3D abs-time

#include <cmath>
#include <algorithm>
#include <type_traits>

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

// Forward declaration for apply_projection_spec() — CAM-03 / DOC 02
// central dispatch helper.  Defined later in this TU as a `static` free
// function, but `eval_pose_tracks` (line ~140) calls it before the
// definition appears in source order, so declare up-front to make the
// signature visible to all callers in this TU.
static void apply_projection_spec(const ProjectionSpec& spec,
                                  const CameraEvalContext& ctx,
                                  Camera2_5D& cam);

// =============================================================================
// COMPILED EVALUATION PATH
// =============================================================================

// ── Free helpers for compiled source dispatch (no header declarations needed) ──

/// Apply orientation from an OrientationSpec variant (free function).
///
/// Agente 1: `path_tangent` and `path_roll_deg` are forwarded from the
/// source evaluator.  When the source is TrajectoryMotion they carry the
/// real tangent and the trajectory point's banking value; for any other
/// source they're std::nullopt and OrientAlongPath falls back to the
/// legacy POI-based contract (NEVER used as a "fake tangent" — just a
/// safe no-op fallback for non-trajectory descriptors).
static void apply_orientation_spec_free(const void* orient_variant,
                                         const CameraEvalContext& ctx,
                                         Camera2_5D& cam,
                                         const std::optional<Vec3>& path_tangent,
                                         const std::optional<float>& path_roll_deg) {
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
    if (auto* oap = std::get_if<OrientAlongPath>(&orient)) {
        // ── A1.3 / A1.4 — Orient camera forward along the REAL tangent ──────
        //
        // When the source evaluator supplied a path_tangent (TrajectoryMotion
        // branch in evaluate_compiled_source) the camera forward is the
        // trajectory tangent — NOT a direction derived from the POI.  This
        // is the cinematic intent: a path that turns a corner must rotate
        // the camera around the curve, regardless of what the per-point
        // POI is set to.  For non-trajectory sources (PoseTracks / Orbit /
        // Static), path_tangent is empty and the orientation step falls
        // back to the legacy POI-based contract (no "fake tangent").
        //
        // ── A1.5 — Banking ─────────────────────────────────────────────
        //   keep_horizon=true ⇒ roll = 0 (level horizon regardless of path banking).
        //   keep_horizon=false + path_roll_deg present (TrajectoryMotion) ⇒ roll
        //     = path_roll_deg (the trajectory point's banking value).
        //   keep_horizon=false + no path_tangent ⇒ roll = cam.rotation.z
        //     (legacy POI contract preserves whatever rotation was set by
        //      the source/modifiers).
        //
        // ── A1.7 — Degenerate-tangent handling ──────────────────────────
        //   The CALLER (CameraProgram::apply_orientation_spec member, see
        //   below) resolves "last valid tangent" fallback BEFORE this
        //   function, so when path_tangent arrives here with length ≤ ε
        //   it means no cached valid tangent is available either; in that
        //   case we simply skip the orientation step and keep the
        //   existing rotation.  Together with upstream NaN-free
        //   quaternion math, this means the camera NEVER reports NaN values.

        if (path_tangent.has_value()) {
            const Vec3& raw = *path_tangent;
            const float len = glm::length(raw);
            if (len > 1e-4f) {
                const Vec3 forward = raw / len;
                const Quat orientation = quat_look_along(forward);
                const float roll = oap->keep_horizon
                    ? 0.0f
                    : (path_roll_deg.has_value() ? *path_roll_deg : cam.rotation.z);
                cam.rotation = quat_to_camera_euler(orientation, roll);
            }
            // path_tangent present but degenerate (len ≤ ε) AND no cached
            // fallback made it through the caller: keep existing rotation,
            // never produce NaN.
            return;
        }

        // No path_tangent (non-trajectory sources): fall back to the legacy
        // POI-based contract — preserves existing tests in §4.D and golden
        // suite.  NEVER reuse the POI as a "fake tangent" — it's only used
        // as a behavioural safety net for non-path sources.
        if (cam.point_of_interest_enabled) {
            Vec3 look_dir = cam.point_of_interest - cam.position;
            float len = glm::length(look_dir);
            if (len > 1e-4f) {
                look_dir = look_dir / len;
                const Quat orientation = quat_look_along(look_dir);
                const float preserved_roll = oap->keep_horizon ? 0.0f : cam.rotation.z;
                cam.rotation = quat_to_camera_euler(orientation, preserved_roll);
            }
        }
        return;
    }
}

/// Evaluate a PoseTracksSource by sampling all animated channels.
///
/// TICKET-022 — orientation is applied once by `CameraProgram::evaluate()`
/// after the modifier pipeline.  See closing block-comment in this TU.
static Camera2_5D eval_pose_tracks(const CameraBaseSpec& base,
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

    // CAM-03 / DOC 02: route the BASE projection through the central
    // dispatch (handles ZoomProjection / FovProjection / PhysicalLensProjection).
    // TICKET-021 — PoseTracksSource must NOT unconditionally force Zoom or
    // stomp the active variant.  Animated pose tracks overlay ONLY the
    // channels owned by the active variant; for PhysicalLensProjection the
    // physical lens carried by the descriptor is canonical and never gets
    // clobbered to base.lens below.
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
            // Pose tracks must NOT touch zoom / fov_deg / lens here:
            // apply_projection_spec already encoded the lens from p.lens
            // and zeroed cam.zoom / cam.fov_deg (so an animated Fov on the
            // pose track cannot leak into a PhysicalLens descriptor).
        } else {
            // TICKET-021 / DOC 02: a new ProjectionSpec variant must update
            // BOTH this std::visit AND apply_projection_spec together.
            // Without this guard the new variant is silently dropped on
            // PoseTracksSource (only the base projection writes cam.zoom /
            // cam.fov_deg / cam.lens, never the pose tracks).
            static_assert(std::is_void_v<T>,
                "PoseTracksSource: unhandled ProjectionSpec variant — "
                "update eval_pose_tracks std::visit AND "
                "apply_projection_spec together.");
        }
    }, base.projection);

    // TICKET-021 / DOC 02: PhysicalLensProjection's lens is the canonical
    // source-of-truth; for Zoom / Fov, fall back to the base lens.
    if (!std::holds_alternative<PhysicalLensProjection>(base.projection)) {
        cam.lens = base.lens;
    }
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

    // TICKET-022 / DOC 02 — orientation is applied ONCE by CameraProgram::evaluate()
    // after the modifier pipeline (canonical order: base → modifier → orientation → constraints).
    // The look-at rotation here would be DERIVED FROM src.position (no modifier offset),
    // then thrown away by evaluate()'s post-modifier re-application.  Call site removed.

    return cam;
}

/// Resolve the active ProjectionSpec into the camera's optics fields.
/// CAM-03 / DOC 02 — central dispatch so adding new variants (e.g.
/// PhysicalLensProjection) does not require duplicate streams of `if/else`
/// across every source evaluator.  Mirrors the contract's optics_mode
/// switches in `camera_math::focal_from_camera()`.
static void apply_projection_spec(const ProjectionSpec& spec,
                                  const CameraEvalContext& ctx,
                                  Camera2_5D& cam) {
    std::visit([&](auto&& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, ZoomProjection>) {
            cam.zoom = p.zoom.evaluate(ctx.sample_time);
            cam.projection_mode = Camera2_5DProjectionMode::Zoom;
            cam.optics_mode = CameraOpticsMode::Zoom;
        } else if constexpr (std::is_same_v<T, FovProjection>) {
            cam.fov_deg = p.fov_deg.evaluate(ctx.sample_time);
            cam.projection_mode = Camera2_5DProjectionMode::Fov;
            cam.optics_mode = CameraOpticsMode::FieldOfView;
        } else if constexpr (std::is_same_v<T, PhysicalLensProjection>) {
            // CAM-03 / DOC 02: physical-lens perspective.
            // The lens's `focal_pixels()` is the runtime focal source-of-truth;
            // zoom and fov_deg from base are intentionally reset here so that
            // any later consumer reading `cam.zoom` or `cam.fov_deg` cannot see
            // a stale Zoom/Fov value coexisting with the lens.  Anamorphic and
            // pixel_aspect live inside LensModel (sensor_width / sensor_height).
            cam.lens = p.lens;
            cam.optics_mode = CameraOpticsMode::PhysicalLens;
            cam.projection_mode = Camera2_5DProjectionMode::Zoom;
            cam.zoom = 0.0f;
            cam.fov_deg = 0.0f;
        }
    }, spec);
}

/// Evaluate an OrbitMotion by sampling the orbit spherical params and
/// translating `track` + `dolly` in the camera-local basis (camera→target
/// forward, world-up-relative right, world-up-relative up).
///
/// TICKET-024 / DOC 02 — PRE-FIX:
///   `track` was added to `target + forward*radius` in WORLD coordinates,
///   and `dolly` was hard-coded to world +Z (`pos.z += dolly`).  Both
///   directives are wrong for an orbit: pitch=90° rotates the camera
///   off-axis and the "lateral" track then pushes the camera in a
///   non-orthogonal world axis, AND `dolly` always pushes AWAY from the
///   target regardless of where the orbit placed the camera.
///
/// TICKET-024 / DOC 02 — POST-FIX:
///   1.  `orbit_position = target + world_forward*radius` (unchanged spherical).
///   2.  `basis_forward = normalize(target - orbit_position)`  (camera→target;
///       equivalent to `-world_forward` for non-degenerate orbits, but the live
///       computation is unit-stable even when radius → 0 or pitch collapses).
///   3.  `basis_right = normalize(cross(basis_forward, world_up))` — right-handed
///       camera-local right; world-X fallback when forward ↔ world-up
///       (pitch = ±90°).
///   4.  `basis_up = cross(basis_right, basis_forward)` (already unit).
///   5.  `pos = orbit_position + track.x * basis_right
///                    + track.y * basis_up
///                    + dolly * basis_forward`.
///       `track.z` is no longer used (was un-scoped "lateral offset" — the
///       OrbitMotion struct docstring says lateral; use `dolly` for any
///       forward push).  The world-Z dollied axis has been removed.
///
/// TICKET-022 — orientation is applied ONCE by `CameraProgram::evaluate()`
/// after the modifier pipeline.  See closing block-comment in this TU.
static Camera2_5D eval_orbit_motion(const CameraBaseSpec& base,
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

    // Spherical forward (target→camera, world frame, unit length by construction).
    const Vec3 world_forward{
        std::cos(pitch_rad) * std::sin(yaw_rad),
        std::sin(pitch_rad),
        std::cos(pitch_rad) * std::cos(yaw_rad)
    };
    const Vec3 orbit_position = target_pos + world_forward * radius;

    // Camera-local basis for track + dolly: derived from the live
    // orbit_position so the basis is unit-stable even when radius → 0
    // (degenerate: orbit_position ≡ target) or pitch → ±90° (basis_forward
    // ∥ world_up triggers the right-axis fallback below).
    Vec3 basis_forward = target_pos - orbit_position;
    float basis_fwd_len = glm::length(basis_forward);
    if (basis_fwd_len > 1e-4f) {
        basis_forward /= basis_fwd_len;
    } else {
        // Degenerate: orbit_position == target.  Fall back to the negated
        // spherical forward (already a unit vector because world_forward
        // is cos²+sin² = 1).  This avoids NaN and preserves the canonical
        // camera→target intent.
        basis_forward = Vec3{
            -world_forward.x,
            -world_forward.y,
            -world_forward.z
        };
    }

    // Right axis = world up × forward (right-handed so camera-local
    // +X maps to world +X when looking down -Z; the look-at-with-up
    // convention used elsewhere in this TU).
    const Vec3 world_up{0.0f, 1.0f, 0.0f};
    Vec3 basis_right = glm::cross(basis_forward, world_up);
    float basis_right_len = glm::length(basis_right);
    if (basis_right_len > 1e-4f) {
        basis_right /= basis_right_len;
    } else {
        // basis_forward ∥ world-up (pitch = ±90°).  Use world +X as a
        // non-NaN fallback; this matches the convention that camera-right
        // aligns with world-X when the camera looks straight down.
        basis_right = Vec3{1.0f, 0.0f, 0.0f};
    }
    // basis_up is already a unit vector (cross of two orthogonal unit vectors).
    Vec3 basis_up = glm::cross(basis_right, basis_forward);

    // Camera-local translations: track.x → right, track.y → up, dolly →
    // camera→target forward.  track.z is no longer used (was un-scoped;
    // see TICKET-024 resolution in docs/FOLLOWUP_TICKETS.md).
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

    // CAM-03: central projection dispatch (handles all 3 variants).
    apply_projection_spec(base.projection, ctx, cam);

    // TICKET-022 / DOC 02 — orientation is applied ONCE by CameraProgram::evaluate()
    // after the modifier pipeline (canonical order: base → modifier → orientation → constraints).
    return cam;
}

// =========================================================================
// evaluate_compiled_source — dispatch to the right evaluator by variant.
// =========================================================================

EvaluatedCameraSource CameraProgram::evaluate_compiled_source(const CameraEvalContext& ctx) const {
    const auto& source = descriptor_.source;
    const auto& base = descriptor_.base;

    EvaluatedCameraSource out;   // All paths default-construct (camera=default, optionals=empty).

    if (auto* pts = std::get_if<PoseTracksSource>(&source)) {
        // TICKET-022 / DOC 02 — orientation is no longer passed to source evaluators;
        // CameraProgram::evaluate() applies it once after modifiers.
        out.camera = eval_pose_tracks(base, *pts, ctx);
        // A1.1 — no path_tangent for non-trajectory sources (orientation step
        // falls back to POI contract for non-path sources).
        return out;
    }
    if (auto* orbit = std::get_if<OrbitMotion>(&source)) {
        bool animated = source_is_time_dependent(source);
        out.camera = eval_orbit_motion(base, *orbit, ctx, animated);
        // A1.1 — no path_tangent for orbit source (POI = orbit target; legacy
        // POI contract applies if the user pairs OrientAlongPath with Orbit).
        return out;
    }
    if (auto* traj = std::get_if<TrajectoryMotion>(&source)) {
        if (traj->trajectory) {
            CameraMotionContext motion_ctx;
            motion_ctx.frame = ctx.frame;
            motion_ctx.sample_time = ctx.sample_time;
            motion_ctx.base_position = base.position;
            motion_ctx.base_target = base.point_of_interest;
            // motion_ctx.base_zoom / .base_fov_deg are unused by the trajectory
            // samplers (they only emit position/tangent/target/roll_deg), so
            // they keep their harmless default values.  Camera optics come
            // from base.projection via apply_projection_spec below (A1.6).
            motion_ctx.base_zoom = 1000.0f;
            motion_ctx.base_fov_deg = 50.0f;
            motion_ctx.base_focus_distance = base.dof.focus_distance;

            auto t = traj->trajectory->sample(motion_ctx);
            Camera2_5D& cam = out.camera;
            cam.enabled = base.enabled;
            cam.position = t.position;

            // CAM-03 / A1.6 — preserve the optics via central projection
            // dispatch (Zoom / Fov / PhysicalLensProjection).  Previously this
            // branch hard-coded `cam.zoom = 1000.0f; cam.fov_deg = 50.0f;`
            // which silently clobbered PhysicalLensProjection — fixed here.
            apply_projection_spec(base.projection, ctx, cam);

            // A1.6 — propagate the rest of the non-motion base state:
            //   lens, DOF, motion blur, parent name.  None of these are
            //   touched by the trajectory itself, so we are simply NOT
            //   dropping them anymore.
            cam.lens            = base.lens;
            cam.dof             = base.dof;
            cam.motion_blur     = base.motion_blur;
            cam.parent_name     = base.parent_name;

            cam.point_of_interest = base.point_of_interest;
            if (t.target) cam.point_of_interest = *t.target;
            cam.point_of_interest_enabled = true;

            // A1.1 — forward the REAL trajectory tangent (parametric
            // direction at this sample) to the orientation step.  Stored
            // raw (not renormalised) here so the orientation step can reuse
            // the normalization it already does in quat_look_along.
            if (glm::length(t.tangent) > 0.0f) {
                out.path_tangent = t.tangent;
            }
            // A1.5 — banking value from the trajectory point (used by the
            // orientation step when keep_horizon=false).
            out.path_roll_deg = t.roll_deg;

            // A1.7 — update the session-cached last valid tangent so a
            // transient degenerate-tangent sample (e.g. start of a Hold
            // segment with zero derivative) doesn't snap the camera to
            // identity on the next evaluate().  Only update when the *raw*
            // tangent has non-trivial length (we keep the raw here so the
            // orientation step's renormalization stays a single site).
            const float raw_len = glm::length(t.tangent);
            if (raw_len > 1e-4f) {
                last_valid_path_tangent_ = t.tangent / raw_len;
                has_last_path_tangent_   = true;
            }

            // TICKET-022 / DOC 02 — orientation is applied ONCE in evaluate() after modifiers.
            return out;
        }
    }

    // StaticCameraSource or unknown: use base.
    Camera2_5D& cam = out.camera;
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

    return out;
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
        // CAM-03 / DOC 02 — single-look-at policy.  When the orientation
        // already provided a look-at (see evaluate() detection block),
        // this constraint branch is silently no-op.  Authors are notified
        // via a Warning diagnostic emitted at evaluate() time.
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

// Agente 1 — the member owns the A1.7 "last valid tangent" fallback cache
// (mutable private state, session-local lifetime) and hands the helper
// either the raw tangent (when non-degenerate) or the cached last-valid
// tangent (when degenerate AND a previous valid sample exists).  The free
// function does the A1.3/A1.4/A1.5/A1.7 NaN-free-safe math on whichever
// tangent survives this selection.
void CameraProgram::apply_orientation_spec(const void* orient_variant,
                                            const CameraEvalContext& ctx,
                                            Camera2_5D& cam,
                                            const std::optional<Vec3>& path_tangent,
                                            const std::optional<float>& path_roll_deg) const {
    // A1.7 — step 1: legacy tangent fallback.  When the source evaluator
    // returned a degenerate path_tangent (length ≤ 1e-4f) AND a previous
    // tangent has been cached for this session/program, route the
    // orientation step through the *last valid* vector.  This prevents
    // the camera from snapping to identity when a transient zero-derivative
    // sample lands (e.g. start of a Hold segment with a zero-length first
    // step, or the very last sample of a closed loop).
    std::optional<Vec3> effective_tangent = path_tangent;
    if (path_tangent.has_value()) {
        const float raw_len = glm::length(*path_tangent);
        if (raw_len <= 1e-4f && has_last_path_tangent_) {
            effective_tangent = last_valid_path_tangent_;
        }
        // (When raw_len > 1e-4f the free helper handles the happy path
        //  directly; when raw_len ≤ 1e-4f AND no cached valid AND no
        //  alternative, the free helper's `if (len > 1e-4f)` skip
        //  implements A1.7 step 2 — keep existing rotation.)
    }

    apply_orientation_spec_free(orient_variant, ctx, cam,
                                effective_tangent, path_roll_deg);
}

// TICKET-022 / DOC 02 — apply_orientation_spec_free() has exactly ONE real
// call site (CameraProgram::evaluate()) per evaluate() invocation.  All source
// evaluators (eval_pose_tracks / eval_orbit_motion / the trajectory branch)
// have been stripped of their pre-modifier orientation calls so the canonical
// order (base → modifier → orientation → constraints) is enforced.
// The camera-program-compiler member `apply_orientation_spec` simply forwards
// to this free function; the member exists so the evaluator can call it as a
// virtual-shaped API surface (matching the public camera_program.hpp
// signature).

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

    // Evaluate source directly (no registry lookup).  Agente 1: the source
    // evaluator now returns an EvaluatedCameraSource so we can thread the
    // real trajectory tangent + banking value into the orientation step.
    EvaluatedCameraSource intermediate_src = evaluate_compiled_source(ctx);
    Camera2_5D& intermediate = intermediate_src.camera;

    // Apply modifiers (PR1: idle oscillation; CAM-04: handheld noise).
    //
    // Both IdleOscillation and HandheldNoise use ABSOLUTE time
    // (`ctx.sample_time.seconds()`) so that two evaluations at the same
    // sample_time — regardless of order or threading — produce identical
    // camera state.  This is the DOC-03 random-access determinism
    // guarantee for the modifier pipeline.
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
        } else if (auto* hh = std::get_if<HandheldNoise>(&mod)) {
            // CAM-04 / DOC 03 — seeded wiggle on ABSOLUTE time.
            const float t_sec = static_cast<float>(ctx.sample_time.seconds());
            // Per-axis 3-channel wiggle for position rotation (decorrelated
            // axes per the same seed; +50u/+150u offsets decorrelate the
            // three vectors used in this modifier).
            const Vec3 pos_off = wiggle3D(
                hh->position_freq_hz, hh->position_amplitude, t_sec, hh->seed);
            const Vec3 rot_off = wiggle3D(
                hh->rotation_freq_hz, hh->rotation_amplitude_deg, t_sec,
                hh->seed + 50u);
            intermediate.position += pos_off;
            intermediate.rotation += rot_off;
            if (hh->zoom_amplitude > 0.0f) {
                intermediate.zoom += wiggle(
                    hh->zoom_freq_hz, hh->zoom_amplitude, t_sec, hh->seed + 150u);
            }
        }
    }

    // CAM-03 / DOC 02 — single-look-at policy detection.
    //
    // Hierarchy rule:  world_orientation = parent ⊗ base ⊗ local_offset.
    //
    // When BOTH an OrientationSpec look-at (LookAtPoint / LookAtLayer) AND
    // a LookAtConstraint are present, the orientation is treated as
    // authoritative (it carries the source-derived target).  A Warning
    // diagnostic is emitted (with the indices of the skipped constraints)
    // and the constraint's look-at branch is skipped via
    // session.skip_look_at_constraint_from_orientation.
    {
        const bool orientation_is_look_at =
            std::holds_alternative<LookAtPoint>(descriptor_.orientation) ||
            std::holds_alternative<LookAtLayer>(descriptor_.orientation);

        // Reviewer minor #2: collect the indices of ALL LookAtConstraint
        // entries (not just any_of bool) so the diagnostic can pinpoint
        // exactly which LookAtConstraint[i] was skipped.  This is what
        // composition authors need to fix their descriptor.
        std::vector<std::size_t> skipped_constraint_indices;
        for (std::size_t i = 0; i < descriptor_.constraints.size(); ++i) {
            if (std::holds_alternative<LookAtConstraint>(descriptor_.constraints[i])) {
                skipped_constraint_indices.push_back(i);
            }
        }

        if (orientation_is_look_at && !skipped_constraint_indices.empty()) {
            session.skip_look_at_constraint_from_orientation = true;

            // Build a human-readable list of skipped indices — "constraints[1]"
            // for single-skip, "constraints[1] and constraints[3]" for two,
            // "constraints[0], constraints[2], and constraints[4]" for >2.
            std::string indices_str;
            for (std::size_t k = 0; k < skipped_constraint_indices.size(); ++k) {
                if (k > 0) {
                    indices_str += (k + 1 < skipped_constraint_indices.size()) ? ", " : " and ";
                }
                indices_str += "constraints[" +
                               std::to_string(skipped_constraint_indices[k]) + "]";
            }

            const char* orient_kind =
                std::holds_alternative<LookAtPoint>(descriptor_.orientation)
                    ? "LookAtPoint"
                    : "LookAtLayer";

            const std::string msg =
                std::string("CAM-03: OrientationSpec ") + orient_kind +
                " and LookAtConstraint present at " + indices_str +
                "; orientation wins (single look-at policy) so the listed "
                "constraint(s) are skipped.";

            result.diagnostics.push_back({
                CameraProgramDiagnostic::Severity::Warning,
                msg
            });
        }
    }

    // Re-apply orientation after modifiers.  Agente 1: forward the
    // trajectory path_tangent + path_roll_deg from the source evaluator so
    // OrientAlongPath can use the *real* tangent (not a POI-derived fake)
    // when paired with TrajectoryMotion.
    apply_orientation_spec(&descriptor_.orientation, ctx, intermediate,
                           intermediate_src.path_tangent,
                           intermediate_src.path_roll_deg);

    // Set is_animated flag.
    intermediate.is_animated = time_dependent_;

    // Carry forward lens and motion blur from descriptor base. The lens
    // must come from PhysicalLensProjection when that variant is active
    // (TICKET-021); otherwise fall back to base.lens. DOF is handled by
    // the source evaluator (PoseTracksSource carries animated DOF
    // channels; OrbitMotion copies base DOF; TrajectoryMotion's DOF
    // coverage is deferred to TICKET-025); do NOT overwrite
    // intermediate.dof here — that would erase animated DOF from
    // PoseTracksSource.
    if (!std::holds_alternative<PhysicalLensProjection>(descriptor_.base.projection)) {
        intermediate.lens = descriptor_.base.lens;
    }

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

// TICKET-022 / DOC 02 — canonical order is enforced:
// base (descriptor.base + descriptor.source via source evaluator)  → modifier
// (descriptor.modifiers loop in evaluate()) → orientation (single site:
// CameraProgram::evaluate() post-modifiers, before constraints) → constraints
// (descriptor.constraints loop in evaluate()).  This file's source evaluators
// (eval_pose_tracks / eval_orbit_motion / the trajectory branch) MUST NOT
// call `apply_orientation_spec_free`; doing so would double-apply look-at at
// the SOURCE position (pre-modifier) and discard it.  See §4.B in
// tests/scene/camera/test_camera_program_compiled.cpp for the regression lock.

} // namespace chronon3d::camera_v1
