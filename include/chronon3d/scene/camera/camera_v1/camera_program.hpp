#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_program.hpp
//
// CameraProgram is the V1 *single evaluation entry-point*.
//
// The RECOMMENDED usage path is:
//   1. Build a CameraDescriptor
//   2. auto program = compile_camera(descriptor, &catalog).value();
//   3. Call program.evaluate(ctx, session) each frame
//
// The builder API (motion(), trajectory(), orient(), banking(), etc.) and
// old evaluate(CameraMotionContext, ConstraintSession) were removed in PR12.
// Use compile_camera() + evaluate(CameraEvalContext, CameraSession) instead.
//
// Orchestration layer over:
//   1. CameraDescriptor + compile_camera()
//   2. Compiled evaluate() with CameraEvalContext + CameraSession
//   3. Source evaluation (static / pose-tracks / orbit / trajectory)
//   4. Orientation + modifiers + constraint spec evaluation
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp>  // TICKET-FRAMING-V1: CameraFramingSolver
#include <chronon3d/core/types/result.hpp>

#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

// Forward declarations for types defined in camera_session.hpp
// which cannot be included here to avoid circular deps.
struct CameraSession;
struct CameraEvalContext;
struct CameraCompileContext;
struct CameraCompileError;
class CameraCatalog;

// =========================================================================
// Diagnostic / result types
// =========================================================================

/// Single diagnostic entry produced during program evaluation.
struct CameraProgramDiagnostic {
    enum class Severity : std::uint8_t { Info = 0, Warning = 1, Error = 2 };
    Severity    severity{Severity::Info};
    std::string message;
};

// =========================================================================
// CameraResolveDiagnostic — ripple-through diagnostic surfaced by
// ShotTimelineResolver (and the OPP renderer integration).
//
// P3-H + TICKET-CAMERA-FULL-LINUX sub-ticket C — 6-field contract:
//
//   camera_id           — descriptor.id of the camera emitting this
//                         diagnostic (probe `CameraDescriptor.id`; if
//                         empty, fall back to the fingerprint's hex form).
//   shot_index          — 0-based shot index in the ShotTimeline; -1 when
//                         the diagnostic originates from a non-timeline
//                         path (e.g. CameraProgram::evaluate() standalone).
//   sample_time         — the canonical SampleTime (seconds, absolute). The
//                         OPP / renderer can sort / filter / fan-out the
//                         diagnostic by sample time even when bursts of
//                         diagnostics collapse onto the same shot index.
//   severity            — Info / Warning / Error (vendor-neutral).
//   code                — canonical short code (e.g. "Uncompiled",
//                         "ConstraintFailure", "LookAtLayerMissingTarget",
//                         "TransitionEvaluationFailed"); suitable for
//                         log-grep / dashboard filtering.
//   message             — human-readable detail (single line; latin-only).
//
// Populated by ShotTimelineResolver::evaluate() per shot evaluation.
// Empty when populated by CameraProgram::evaluate() directly (program-level
// standalone path) — the OPP renderer reads `EvaluatedCamera::diagnostics`
// for the program-level entries and `EvaluatedCamera::resolve_diagnostics`
// for the timeline-level enriched entries.
//
// Cat-3 justified per TICKET-CAMERA-FULL-LINUX sub-ticket C user spec
// verbatim (the 6-field contract is the canonical renderer-facing surface;
// `CameraProgramDiagnostic` remains the program-level surface).
// =========================================================================
struct CameraResolveDiagnostic {
    // Severity is an ALIAS to CameraProgramDiagnostic::Severity (both
    // share the same 3 values: Info / Warning / Error).  Using the alias
    // rather than re-declaring makes the bit-cast in
    // `enrich_resolve_diagnostics` (src/scene/camera/camera_v1/shot_timeline.cpp)
    // a guaranteed type-level coupling — the two enums cannot diverge.
    using Severity = CameraProgramDiagnostic::Severity;
    std::string camera_id;
    int         shot_index{-1};
    double      sample_time_seconds{0.0};
    Severity    severity{Severity::Info};
    std::string code;
    std::string message;
};

/// Structured success result from CameraProgram::evaluate() / ShotTimelineResolver::evaluate().
/// Carries the evaluated camera plus non-fatal diagnostics (warnings, infos).
/// Fatal errors are returned as CameraEvaluationError via Result.
///
/// `resolve_diagnostics` is the timeline-level ripple-through surface with
/// the 6-field contract (camera_id + shot_index + sample_time + severity
/// + code + message). CameraProgram::evaluate() leaves it EMPTY (program
/// path is timeline-agnostic); ShotTimelineResolver::evaluate() fills it
/// one-to-one with `diagnostics`, enriching each program-level entry
/// with shot_index + sample_time + camera_id. The OPP renderer reads
/// this surface to forward diagnostics to its output sink without
/// re-deriving the ripple-through fields per consumer.
struct EvaluatedCamera {
    Camera2_5D                                  camera;
    std::vector<CameraProgramDiagnostic>        diagnostics;
    std::vector<CameraResolveDiagnostic>        resolve_diagnostics;
};

/// Discrete error codes returned by CameraProgram + ShotTimeline
/// evaluation paths.  Phase 1.C (TICKET-120): split this out of the
/// per-struct `Kind` enum so that ShotTimelineResolver::evaluate can
/// surface its OWN distinct failure (`TransitionEvaluationFailed`) without
/// having to inherit CameraProgram's discriminator.  `CameraEvaluationError`
/// now carries a `CameraErrorCode code` (no nested `Kind`) — see
/// CHANGELOG_ARCHIVE.md §TICKET-120 Sub-commit E + the user's Phase 1.C
/// spec for the rationale.
enum class CameraErrorCode : std::uint8_t {
    Unknown = 0,
    Uncompiled,                  // evaluate() called before compile_camera()
    ConstraintFailure,           // a constraint failed with Stop policy
    TransitionEvaluationFailed,  // Phase 1.C: ShotTimelineResolver::evaluate
                                 // could not propagate a structured error
                                 // from a transition-time program evaluation.
};

/// Fatal error from CameraProgram::evaluate() / ShotTimelineResolver::evaluate().
/// Returned via Result<EvaluatedCamera, CameraEvaluationError>.
struct CameraEvaluationError {
    CameraErrorCode code{CameraErrorCode::Unknown};
    std::string    message;
};

/// Source evaluation result — carries the evaluated camera plus optional
/// trajectory tangent and roll that the orientation stage (OrientAlongPath)
/// needs.  For non-trajectory sources, tangent and roll_deg are nullopt.
struct EvaluatedCameraSource {
    Camera2_5D              camera;
    std::optional<Vec3>     tangent;
    std::optional<float>    roll_deg;
};

// =========================================================================
// CameraEvaluationDependency — CAM-02 metadata.
//
// Classifies whether the compiled program requires persistent state across
// frames to evaluate correctly.  Surfaced on CameraProgram via
// `evaluation_dependency()` so the runtime / scheduler can decide whether
// the program may be safely evaluated in a stateless fast-path (e.g. a
// worker pool that does not preserve CameraSession identity across calls)
// or whether it must always be evaluated with the SAME CameraSession.
//
//   - Stateless          : pure function of (descriptor, CameraEvalContext).
//                          No state is read or written across frames.
//   - RequiresHistory    : the program mutates a CameraSession's
//                          ConstraintState (e.g. DampedFollowConstraint's
//                          EMA accumulator) or banking_roll across
//                          frames; identity-preserving evaluation is
//                          required.
//
// The compiler (camera_program_compiler.cpp) sets this field from a
// survey of the descriptor's source / constraints / modifiers.  Pure
// modifiers (IdleOscillation — sin(t*freq)) are Stateless even with
// amplitude ≠ 0 because the formula is f(ctx.sample_time).
// =========================================================================

enum class CameraEvaluationDependency : std::uint8_t {
    Stateless      = 0,
    RequiresHistory = 1,
};

// =========================================================================
// CameraProgramKind — TICKET-PHASE-2 (Phase 2 refactor):
// source-kind discriminator for a compiled CameraProgram.
//
// Re-populated by compile_camera() AFTER EVERY GRAFT (including
// RegisteredMotionRef resolution), since the resolved source can differ
// from the descriptor's declared source.  This is the structural classifier
// used by the runtime to pick evaluation strategies (e.g. PoseTracks path
// vs. trajectory path vs. static fast-path).
// =========================================================================
enum class CameraProgramKind : std::uint8_t {
    Static      = 0,  // resolved to StaticCameraSource
    PoseTracks  = 1,  // resolved to PoseTracksSource
    Orbit       = 2,  // resolved to OrbitMotion
    Trajectory  = 3,  // resolved to TrajectoryMotion
    Ref         = 4,  // UNRESOLVED RegisteredMotionRef (only valid on failure)
    CameraMotionParams = 5,  // TICKET-P2-29: resolved to CameraMotionParamsSource
};

// =========================================================================
// CameraCompileErrorCode — TICKET-PHASE-2 (Phase 2 refactor):
// top-level structured error code from compile_camera().
//
// Replaces the previous nested `CameraCompileError::Kind` enum.  The 11
// values consolidate the 20+ legacy Kind variants into the canonical
// buckets called out by the Phase 2 spec:
//
//   - InvalidDescriptor        : empty id, orient-without-trajectory, etc.
//   - InvalidProjection        : Fov/Zoom keyframes out of [0, 179)
//   - InvalidLens              : focal_length, sensor, pixel_aspect,
//                                anamorphic_squeeze, motion_blur fields
//   - InvalidTrajectory        : empty/null trajectory, segment duration
//   - InvalidSegmentIndex      : segment from_idx >= to_idx or OOB
//   - MissingPreset            : RegisteredMotionRef not in catalog
//   - CircularPresetReference  : catalog cycle in RegisteredMotionRef chain
//   - MissingTarget            : LookAtLayer with empty target string
//   - MissingParent            : forward-point (never emitted in Phase 2)
//   - InvalidConstraint        : DistanceConstraint min > max
//   - NonFiniteValue           : numeric field is NaN or Inf
// =========================================================================
enum class CameraCompileErrorCode : std::uint8_t {
    InvalidDescriptor       = 0,
    InvalidProjection       = 1,
    InvalidLens             = 2,
    InvalidTrajectory       = 3,
    InvalidSegmentIndex     = 4,
    MissingPreset           = 5,
    CircularPresetReference = 6,
    MissingTarget           = 7,
    MissingParent           = 8,  // forward-point; never emitted in Phase 2
    InvalidConstraint       = 9,
    NonFiniteValue          = 10,
};

// kCameraProgramSchemaVersion is now defined in
// <chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp>
// to break a circular include chain.  It is available here via the
// transitive include: camera_program.hpp -> camera_descriptor.hpp ->
// camera_descriptor_fingerprint.hpp.

// =========================================================================
// CameraProgram
// =========================================================================

class CameraProgram {
public:
    CameraProgram() = default;

    // ── Compiled evaluate ───────────────────────────────────────────────
    /// Evaluate the compiled program using a slim context (no base state
    /// duplication).  The base state is read from the internal descriptor.
    /// Only valid after compile_camera(); calls without compilation will
    /// produce an error diagnostic.
    [[nodiscard]] ::chronon3d::Result<EvaluatedCamera, CameraEvaluationError>
    evaluate(const CameraEvalContext& ctx,
             CameraSession& session) const;

    // ── Compiled state queries ──────────────────────────────────────────
    /// True after compile_camera() has populated this program.
    bool is_compiled() const { return compiled_; }

    /// Access the compiled descriptor (for inspection / serialisation).
    /// Returns nullptr if not compiled.
    const CameraDescriptor* descriptor() const {
        return compiled_ ? &descriptor_ : nullptr;
    }

    /// True if the camera is time-dependent (for caching).
    bool is_time_dependent() const { return time_dependent_; }

    // ── TICKET-PHASE-2 accessors for the new metadata fields ──────────────
    CameraProgramKind program_kind() const { return program_kind_; }
    std::uint64_t     fingerprint()  const { return fingerprint_; }

    /// CAM-02: classify whether the compiled program requires persistent
    /// state (CameraSession) across frames.  See `CameraEvaluationDependency`
    /// above for the per-variant classification rules applied by the compiler.
    CameraEvaluationDependency evaluation_dependency() const {
        return evaluation_dependency_;
    }

private:
    friend ::chronon3d::Result<CameraProgram, CameraCompileError>
    compile_camera(const CameraDescriptor&, const CameraCatalog*, CameraCompileContext&);

    // ── Runtime state ───────────────────────────────────────────────────
    CameraFailurePolicy        failure_policy_{CameraFailurePolicy::Stop};

    // ── Compiled state (populated by compile_camera()) ───────────────────
    // TICKET-PHASE-2: all 5 metadata fields below are re-populated at the
    // END of compile_camera() — AFTER every graft (including
    // RegisteredMotionRef resolution) and AFTER every validation pass.
    // This enforces the "always re-populate after every graft" invariant
    // from the Phase 2 spec: no early return may leave stale metadata.
    bool                              compiled_{false};
    bool                              time_dependent_{false};
    CameraEvaluationDependency        evaluation_dependency_{
        CameraEvaluationDependency::Stateless};
    CameraProgramKind                 program_kind_{CameraProgramKind::Static};
    std::uint64_t                     fingerprint_{0};  // compute_camera_descriptor_fingerprint(descriptor_)
    CameraDescriptor                  descriptor_{};

    // ── Compiled evaluation helpers ─────────────────────────────────────
    /// Evaluate a source variant directly (no registry lookup).
    EvaluatedCameraSource evaluate_compiled_source(const CameraEvalContext& ctx) const;

    /// Apply orientation from an OrientationSpec variant (passed as opaque ptr).
    std::optional<CameraProgramDiagnostic> apply_orientation_spec(
        const void* orient_variant,
        const CameraEvalContext& ctx,
        Camera2_5D& cam,
        const std::optional<Vec3>& tangent,
        const std::optional<float>& roll_deg,
        CameraSession& session) const;

    // TICKET-FRAMING-V1: framing solver member.  `mutable` so the const
    // `evaluate()` can thread the solver through (the solver's `solve()`
    // is logically const but the per-call `FramingSession` is owned by
    // the caller — see `CameraSession::framing_session`).  The solver
    // itself is stateless; `solve()` only mutates the session argument,
    // not the solver.  Reusing one solver across evaluations avoids
    // per-frame construction cost.
    mutable CameraFramingSolver framing_solver_;
};

} // namespace chronon3d::camera_v1
