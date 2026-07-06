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

#include "camera_program_sources.hpp"  // FASE 4 Step 1 — source eval helpers
#include "camera_program_constraints.hpp"  // FASE 4 Step 2 — constraint eval

#include <cmath>
#include <algorithm>
#include <type_traits>

namespace chronon3d::camera_v1 {

// =============================================================================
// COMPILED EVALUATION PATH
// =============================================================================

// ── Free helpers for compiled source dispatch (no header declarations needed) ──

/// Apply orientation from an OrientationSpec variant (free function).
/// Returns an optional diagnostic (e.g. warning for degenerate tangent fallback).
static std::optional<CameraProgramDiagnostic> apply_orientation_spec_free(
    const void* orient_variant,
    const CameraEvalContext& ctx,
    Camera2_5D& cam,
    const std::optional<Vec3>& tangent,
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
            cam.rotation = quat_to_camera_euler(orientation, cam.rotation.z);
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
                    cam.rotation = quat_to_camera_euler(orientation, cam.rotation.z);
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
        // Fallback chain for degenerate tangents:
        //   1. Use the trajectory's tangent if non-degenerate.
        //   2. Use the session's last_tangent (preserved from a prior frame).
        //   3. Use the direction toward point_of_interest (if enabled).
        //   4. Keep the base rotation (no-op).
        // Each fallback step emits a Warning diagnostic so composition
        // authors can identify the problem.

        Vec3 fwd;
        bool have_tangent = false;
        std::string fallback_reason;

        // Step 1: trajectory tangent.
        if (tangent && glm::length(*tangent) > 1e-6f) {
            fwd = glm::normalize(*tangent);
            have_tangent = true;
            session.last_tangent = fwd;  // preserve for future degenerate frames
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
            // Step 4: keep base rotation — no orientation change.
            return CameraProgramDiagnostic{
                CameraProgramDiagnostic::Severity::Warning,
                "OrientAlongPath: no valid tangent, previous tangent, or POI direction — keeping base rotation"
            };
        }

        const Quat orientation = quat_look_along(fwd);
        // OrientAlongPath: the trajectory controls roll, so we do NOT
        // preserve the base rotation's roll.  Pass 0.0f to clear it,
        // then set the roll exclusively from the trajectory sample.
        cam.rotation = quat_to_camera_euler(orientation, 0.0f);

        // Replace roll with trajectory roll (override, not add).
        if (!oap->keep_horizon && roll_deg) {
            cam.rotation.z = *roll_deg;
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

// ── apply_orientation_spec member delegates to free function ────────────────

std::optional<CameraProgramDiagnostic> CameraProgram::apply_orientation_spec(
    const void* orient_variant,
    const CameraEvalContext& ctx,
    Camera2_5D& cam,
    const std::optional<Vec3>& tangent,
    const std::optional<float>& roll_deg,
    CameraSession& session) const {
    return apply_orientation_spec_free(orient_variant, ctx, cam, tangent, roll_deg, session);
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

chronon3d::Result<EvaluatedCamera, CameraEvaluationError>
CameraProgram::evaluate(const CameraEvalContext& ctx,
                        CameraSession& session) const {
    EvaluatedCamera result;

    if (!compiled_) {
        return CameraEvaluationError{
            CameraEvaluationError::Kind::Uncompiled,
            "CameraProgram not compiled — call compile_camera() first"
        };
    }

    // Evaluate source directly (no registry lookup).
    auto evaluated_source = evaluate_compiled_source(ctx);
    Camera2_5D intermediate = evaluated_source.camera;

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

    // Re-apply orientation after modifiers, passing trajectory tangent/roll
    // for OrientAlongPath support.
    auto orient_diag = apply_orientation_spec(
        &descriptor_.orientation, ctx, intermediate,
        evaluated_source.tangent, evaluated_source.roll_deg, session);
    if (orient_diag) {
        result.diagnostics.push_back(*orient_diag);
    }

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
                // CAM-03: Stop = true failure.  Return error.
                return CameraEvaluationError{
                    CameraEvaluationError::Kind::ConstraintFailure,
                    "constraint[" + std::to_string(i) + "] failed: " + cr.reason
                };
            case CameraFailurePolicy::KeepLastValidCamera:
                // ──────────────────────────────────────────────────────────────────────
                // TICKET-A3-SESSION-POLICY (Agent3 mission DoD gate (c)):
                // this is the SOLE wire of `CameraSession::last_valid_camera` in
                // the evaluator.  The field is WRITTEN at the END of evaluate()
                // on every successful pass (the final 2 lines below this switch);
                // this case READS it on failure and returns the cached camera as
                // the recovery snapshot.  Without this wire, KeepLastValidCamera
                // would behave identically to Stop — the Agent3 mission
                // describes exactly this regression (problem #2: "KeepLastValidCamera
                // segue lo stesso ramo di Stop").  This sentinel must NOT be
                // removed silently in future refactors.
                //
                //   • With a cached valid camera → emit ONE recovery Warning that
                //     names both the failed constraint index and the reason, and
                //     return EvaluatedCamera with the cached camera.  The cache
                //     is preserved (already holds the same value) for subsequent
                //     recoveries — no re-write needed inside this branch.
                //   • Without a cached valid camera (first-encounter, or
                //     session.reset() between evals) → fall through to true
                //     ConstraintFailure so the caller can re-pre-roll or surface
                //     the broken composition upstream.
                //
                // Regression lock: tests/runtime/test_camera_session_keep_last_valid.cpp
                // — 2-frame scenario (Frame 0 passes + Frame 1 fails) covering
                // both branches of this case.
                // ──────────────────────────────────────────────────────────────────────
                if (session.last_valid_camera.has_value()) {
                    result.camera = *session.last_valid_camera;
                    result.diagnostics.push_back({
                        CameraProgramDiagnostic::Severity::Warning,
                        std::string("Recovered: constraint ") + std::to_string(i) +
                            " failed (" + cr.reason + "); using last valid camera"
                    });
                    return result;
                }
                // No cached valid camera to recover — true error (same kind as Stop).
                return CameraEvaluationError{
                    CameraEvaluationError::Kind::ConstraintFailure,
                    "constraint[" + std::to_string(i) + "] failed: " + cr.reason +
                        " (KeepLastValidCamera policy, but no valid camera cached)"
                };
            case CameraFailurePolicy::SkipFailedConstraint:
                continue;
            }
        }
        intermediate = cr.camera;
    }

    result.camera = intermediate;
    // CAM-03: persist the last camera that passed all constraints.
    // Used by KeepLastValidCamera policy on subsequent failures.
    session.last_valid_camera = result.camera;
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
