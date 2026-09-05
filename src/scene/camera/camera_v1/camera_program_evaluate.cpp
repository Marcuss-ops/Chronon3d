// ═══════════════════════════════════════════════════════════════════════════
// camera_program_evaluate.cpp — CameraProgram::evaluate — modifiers, look-at policy, constraints, framing, validation
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
// compiled evaluate() — no registry lookup, no mutex.
// =========================================================================

chronon3d::Result<EvaluatedCamera, CameraEvaluationError>
CameraProgram::evaluate(const CameraEvalContext& ctx,
                        CameraSession& session) const {
    EvaluatedCamera result;

    if (!compiled_) {
        return CameraEvaluationError{
            CameraErrorCode::Uncompiled,
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
                    CameraErrorCode::ConstraintFailure,
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
                // No cached valid camera to recover — true error (same code as Stop).
                return CameraEvaluationError{
                    CameraErrorCode::ConstraintFailure,
                    "constraint[" + std::to_string(i) + "] failed: " + cr.reason +
                        " (KeepLastValidCamera policy, but no valid camera cached)"
                };
            case CameraFailurePolicy::SkipFailedConstraint:
                continue;
            }
        }
        intermediate = cr.camera;
    }

    // TICKET-FRAMING-V1: 5th-stage framing (after constraints, before
    // final return).  Opt-in: runs only when the descriptor has
    // non-empty `framing_targets` in `CameraBaseSpec`.  The framing
    // solver picks the camera position + aim that frames all targets
    // within the safe area + rule-of-thirds + dead-zone constraints.
    // The solver's per-frame state (previous aim, smoothed dolly,
    // hysteresis EMA) is held in `session.framing_session` and
    // persists across evaluations for stable on-screen motion.
    //
    // HONEST GAP: the per-layer "real bounds" query is NOT implemented;
    // the evaluator reads the targets from `descriptor_.base.framing_targets`
    // which the composition author fills in at descriptor-build time.
    // A real-bounds resolver (against `ctx.transforms` or a new
    // scene-bounds resolver) is catalogued as a forward-point in
    // `docs/FOLLOWUP_TICKETS.md` §Catalogued forward-points.
    if (!descriptor_.base.framing_targets.empty()) {
        CameraFramingRequest req;
        req.targets = descriptor_.base.framing_targets;
        req.composition_point = descriptor_.base.composition_point;
        req.look_ahead = descriptor_.base.look_ahead;
        req.viewport = Viewport{static_cast<float>(ctx.viewport_width), static_cast<float>(ctx.viewport_height)};
        const CameraFramingResult framing_result =
            framing_solver_.solve(req, intermediate, session.framing_session);
        intermediate = framing_result.camera;
    }

    // TICKET-FRAMING-V1: 7th-stage validation finale (end-of-pipeline
    // NaN/Inf sanity check on the final camera state).  Per the
    // design validation (thinker Q-A), this reuses the existing
    // `CameraErrorCode::ConstraintFailure` discriminator (no new public
    // symbol) and emits a descriptive message naming the failing field.
    // The check fires AFTER framing so a solver that produces a
    // degenerate state (e.g. NaN from a divide-by-zero in a degenerate
    // bounding box) is caught before the renderer sees it.
    auto has_nan_or_inf = [](const Vec3& v) {
        return std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z) ||
               std::isinf(v.x) || std::isinf(v.y) || std::isinf(v.z);
    };
    if (has_nan_or_inf(intermediate.position)) {
        return CameraEvaluationError{
            CameraErrorCode::ConstraintFailure,
            "validation finale: NaN/Inf in final camera position"
        };
    }
    if (has_nan_or_inf(intermediate.rotation)) {
        return CameraEvaluationError{
            CameraErrorCode::ConstraintFailure,
            "validation finale: NaN/Inf in final camera rotation"
        };
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
