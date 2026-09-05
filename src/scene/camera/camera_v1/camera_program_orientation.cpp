// ═══════════════════════════════════════════════════════════════════════════
// camera_program_orientation.cpp — OrientationSpec application for the compiled evaluation path
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

namespace {
    // TICKET-CAM-QUAT-PRIMARY: look-ahead delta for OrientAlongPath's
    // Step 1b fallback. 1/60 s = 1 frame at 60 fps; short enough to
    // track real scene motion, long enough to anticipate a single-frame
    // tangent degeneracy. File-local constant (not in public header)
    // because the delta is an internal implementation detail of the
    // Step 1b wiring; future callers wanting a different delta can
    // thread it through as a parameter.
    constexpr float kOrientAlongPathLookAheadDeltaSeconds = 1.0f / 60.0f;
}

// ── Free helpers for compiled source dispatch (no header declarations needed) ──

/// Apply orientation from an OrientationSpec variant (free function).
/// Returns an optional diagnostic (e.g. warning for degenerate tangent fallback).
///
/// TICKET-CAM-QUAT-PRIMARY concern 2 closure: `look_ahead_tangent` parameter
/// is the look-ahead tangent computed by the caller (the member overload
/// below) via `look_ahead_tangent(descriptor_.source, ctx, delta_seconds)`.
/// The free function uses it in Step 1b when the current tangent is
/// degenerate. The free function is intentionally source-agnostic (it only
/// reads the OrientationSpec variant); the member overload is the source-aware
/// controller that threads the look-ahead through.
static std::optional<CameraProgramDiagnostic> apply_orientation_spec_free(
    const void* orient_variant,
    const CameraEvalContext& ctx,
    Camera2_5D& cam,
    const std::optional<Vec3>& tangent,
    const std::optional<Vec3>& look_ahead_tangent,
    const std::optional<float>& roll_deg,
    CameraSession& session) {
    const auto& orient = *static_cast<const OrientationSpec*>(orient_variant);

    if (std::holds_alternative<FixedOrientation>(orient)) {
        return std::nullopt;
    }
    if (auto* lap = std::get_if<LookAtPoint>(&orient)) {
        Vec3 look_dir = lap->target - cam.position;
        float len = glm::length(look_dir);
        if (len > 1e-4f) {
            look_dir = look_dir / len;
            const Quat orientation = quat_look_along(look_dir);
            // LookAt owns the full orientation.  Do not preserve a residual
            // modifier roll here; roll is represented explicitly by the
            // orientation/constraint contract.
            cam.rotation = quat_to_camera_euler(orientation, 0.0f);
            cam.point_of_interest = lap->target;
            cam.point_of_interest_enabled = true;
        }
        return std::nullopt;
    }
    if (auto* lal = std::get_if<LookAtLayer>(&orient)) {
        // TICKET-A3-LOOKAT-DIAGNOSTIC (Agent3 mission DoD gate (g)) —
        // when the transform snapshot is unavailable OR the named
        // layer does not resolve, emit a Warning diagnostic via the
        // canonical channel (this `std::optional<CameraProgramDiagnostic>`
        // return → `CameraProgram::evaluate()` pushes onto
        // `result.diagnostics`) instead of silent fallback.  The
        // diagnostic message carries a stable `[MissingTransforms]`
        // prefix so test code can grep for it without false positives
        // against unrelated rotation/fallback messages.
        if (ctx.transforms) {
            auto world_pos = ctx.transforms->world_position(lal->target);
            if (world_pos) {
                Vec3 look_dir = *world_pos - cam.position;
                float len = glm::length(look_dir);
                if (len > 1e-4f) {
                    look_dir = look_dir / len;
                    const Quat orientation = quat_look_along(look_dir);
                    cam.rotation = quat_to_camera_euler(orientation, 0.0f);
                    cam.point_of_interest = *world_pos;
                    cam.point_of_interest_enabled = true;
                }
                return std::nullopt;
            }
            // transforms is non-null but world_position returned
            // nullopt (target layer is not in the snapshot OR has no
            // world position); emit a Warning diagnostic and leave
            // cam.rotation at its descriptor base.
            return CameraProgramDiagnostic{
                CameraProgramDiagnostic::Severity::Warning,
                std::string("[MissingTransforms] LookAtLayer target='") +
                    lal->target +
                    "': not resolved (world_position returned nullopt); "
                    "rotation not updated."
            };
        }
        // transforms is nullptr — canonical missing-transforms case.
        return CameraProgramDiagnostic{
            CameraProgramDiagnostic::Severity::Warning,
            std::string("[MissingTransforms] LookAtLayer target='") +
                lal->target +
                "': CameraEvalContext::transforms is nullptr; "
                "rotation not updated."
        };
    }
    if (auto* oap = std::get_if<OrientAlongPath>(&orient)) {
        // OrientAlongPath — orient the camera along the trajectory tangent.
        //
        // TICKET-CAM-QUAT-PRIMARY: 4-level fallback chain (canonical user-spec):
        //   1. Use the trajectory's CURRENT tangent if non-degenerate.
        //   1b. If the current tangent is degenerate, sample t + Δ (look-ahead)
        //       and use the look-ahead tangent if it is non-degenerate.
        //   2. Use the session's last_tangent (preserved from a prior frame).
        //   3. Use the direction toward point_of_interest (if enabled).
        //   4. Use the session's last_orientation Quat (frame-continuity)
        //      if available, else keep the base rotation (no-op).
        // Each fallback step emits a Warning diagnostic so composition
        // authors can identify the problem.  Step 4's "last orientation
        // Quat" is the frame-continuity hook that the prior Euler-only
        // path lacked (the prior path would jump 179° → -179° on a
        // degenerate frame recovery — the Quat path is shortest-arc).

        // We need the source for the look-ahead helper.  The free function
        // is invoked via the wrapper that holds it.  Since this is a free
        // helper with no access to `descriptor_`, we look up the source
        // indirectly via the caller's chain: the source evaluator sets
        // `cam.is_animated` for non-static sources.  For the look-ahead
        // hook, we use a no-op when the source is not a TrajectoryMotion
        // (the helper returns `used=false` for non-trajectory sources).
        // The descriptor source is accessible via the camera_program.hpp
        // declaration `apply_orientation_spec(orient, ctx, cam, ...)`
        // member which has access to `descriptor_`.  For this free
        // function, the look-ahead is a no-op (graceful degradation).

        Vec3 fwd;
        bool have_tangent = false;
        bool used_look_ahead = false;
        std::string fallback_reason;

        // Step 1: trajectory current tangent.
        if (tangent && glm::length(*tangent) > 1e-6f) {
            fwd = glm::normalize(*tangent);
            have_tangent = true;
            session.last_tangent = fwd;  // preserve for future degenerate frames
        }
        // Step 1b: look-ahead (TICKET-CAM-QUAT-PRIMARY concern 2 closure).
        // The caller (member overload) computes the look-ahead tangent by
        // sampling the trajectory at ctx.sample_time + Δ and threads it
        // through this `look_ahead_tangent` parameter.  When the current
        // tangent is degenerate, we substitute the look-ahead tangent if
        // it is non-degenerate; this gives the camera a brief future-aware
        // anticipation window before falling back to last_tangent (Step 2).
        else if (look_ahead_tangent && glm::length(*look_ahead_tangent) > 1e-6f) {
            fwd = glm::normalize(*look_ahead_tangent);
            have_tangent = true;
            used_look_ahead = true;
            // `last_tangent` is the most recent successful tangent (Step 1
            // OR Step 1b look-ahead), NOT strictly the previous frame. This
            // dual-write semantic lets a subsequent degenerate frame skip
            // the look-ahead and recover the look-ahead-derived tangent
            // directly via Step 2 (no new field, no API change).
            session.last_tangent = fwd;
            fallback_reason = "OrientAlongPath: current tangent degenerate, using look-ahead tangent (t+Δ)";
        }
        // Step 2: session's last_tangent.
        else if (session.last_tangent && glm::length(*session.last_tangent) > 1e-6f) {
            fwd = glm::normalize(*session.last_tangent);
            have_tangent = true;
            fallback_reason = "OrientAlongPath: current tangent degenerate, using previous frame tangent";
        }
        // Step 3: direction toward POI.
        else if (cam.point_of_interest_enabled) {
            Vec3 to_poi = cam.point_of_interest - cam.position;
            float poi_len = glm::length(to_poi);
            if (poi_len > 1e-6f) {
                fwd = glm::normalize(to_poi);
                have_tangent = true;
                fallback_reason = "OrientAlongPath: tangent degenerate, using direction toward point_of_interest";
            }
        }

        if (!have_tangent) {
            // Step 4: frame-continuity Quat (TICKET-CAM-QUAT-PRIMARY).  If
            // we have a preserved last_orientation Quat from a prior
            // non-degenerate frame, reuse it (shortest-arc; no jump).
            if (session.last_orientation) {
                cam.rotation = glm::degrees(glm::eulerAngles(*session.last_orientation));
                // Preserve the prior roll (no-op when trajectory is absent).
                return CameraProgramDiagnostic{
                    CameraProgramDiagnostic::Severity::Warning,
                    "OrientAlongPath: no valid tangent / last_tangent / POI direction — reusing last frame orientation (Quat)"
                };
            }
            // No preserved Quat — keep base rotation (no-op).
            return CameraProgramDiagnostic{
                CameraProgramDiagnostic::Severity::Warning,
                "OrientAlongPath: no valid tangent, previous tangent, or POI direction — keeping base rotation"
            };
        }

        // TICKET-CAM-QUAT-PRIMARY: compute the orientation as a Quat, write
        // the Quat to the session for frame-continuity, and only convert
        // to Euler at the boundary (the `cam.rotation` field below).  This
        // avoids the 179° → -179° jump that the prior Euler-only path
        // suffered on `quat_to_camera_euler(...)` near the singularity.
        const Quat orientation = quat_look_along(fwd);
        // Shortest-arc vs the prior orientation (frame continuity) — if
        // the dot product is negative, the result would take the long way
        // around; flip to take the shortest arc.
        Quat frame_oriented = orientation;
        if (session.last_orientation) {
            const float d = glm::dot(frame_oriented, *session.last_orientation);
            if (d < 0.0f) {
                frame_oriented = -frame_oriented;  // negate the w-component via -Quat
            }
        }
        // Write to the session for the next frame's continuity check.
        session.last_orientation = glm::normalize(frame_oriented);

        // Convert Quat → Euler ONLY at the boundary (legacy/diagnostic
        // surface).  Pass 0.0f to clear the base roll (OrientAlongPath
        // controls roll exclusively from the trajectory).
        cam.rotation = quat_to_camera_euler(frame_oriented, 0.0f);

        // Replace roll with trajectory roll (override, not add).
        if (!oap->keep_horizon && roll_deg) {
            cam.rotation.z = *roll_deg;
        }

        if (used_look_ahead) {
            return CameraProgramDiagnostic{
                CameraProgramDiagnostic::Severity::Info,
                fallback_reason.empty()
                    ? std::string("OrientAlongPath: current tangent degenerate, using look-ahead tangent (t+Δ)")
                    : fallback_reason
            };
        }
        if (!fallback_reason.empty()) {
            return CameraProgramDiagnostic{
                CameraProgramDiagnostic::Severity::Warning,
                fallback_reason
            };
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// ── apply_orientation_spec member delegates to free function ────────────────

std::optional<CameraProgramDiagnostic> CameraProgram::apply_orientation_spec(
    const void* orient_variant,
    const CameraEvalContext& ctx,
    Camera2_5D& cam,
    const std::optional<Vec3>& tangent,
    const std::optional<float>& roll_deg,
    CameraSession& session) const {
    // TICKET-CAM-QUAT-PRIMARY concern 2 closure: compute the look-ahead
    // tangent ONLY when the descriptor orientation is `OrientAlongPath` (the
    // only case where look-ahead is meaningful — it is a TrajectoryMotion
    // concept).  The helper returns `used=false` for non-trajectory sources,
    // so this is a safe no-op for PoseTracksSource / OrbitMotion / Static
    // / RegisteredMotionRef.  Default delta = 1/60 s = 1 frame at 60 fps
    // (short enough to track real scene motion; long enough to anticipate
    // a single-frame tangent degeneracy).
    std::optional<Vec3> look_ahead;
    if (std::holds_alternative<OrientAlongPath>(descriptor_.orientation)) {
        const LookAheadResult la =
            look_ahead_tangent(descriptor_.source, ctx, kOrientAlongPathLookAheadDeltaSeconds);
        if (la.used) {
            look_ahead = la.tangent;
        }
    }
    return apply_orientation_spec_free(
        orient_variant, ctx, cam, tangent, look_ahead, roll_deg, session);
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

} // namespace chronon3d::camera_v1
