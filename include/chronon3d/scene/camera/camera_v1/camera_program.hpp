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

/// Structured success result from CameraProgram::evaluate().
/// Carries the evaluated camera plus non-fatal diagnostics (warnings, infos).
/// Fatal errors are returned as CameraEvaluationError via Result.
struct EvaluatedCamera {
    Camera2_5D                              camera;
    std::vector<CameraProgramDiagnostic>    diagnostics;
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
    [[nodiscard]] chronon3d::Result<EvaluatedCamera, CameraEvaluationError>
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

    /// CAM-02: classify whether the compiled program requires persistent
    /// state (CameraSession) across frames.  See `CameraEvaluationDependency`
    /// above for the per-variant classification rules applied by the compiler.
    CameraEvaluationDependency evaluation_dependency() const {
        return evaluation_dependency_;
    }

private:
    friend chronon3d::Result<CameraProgram, CameraCompileError>
    compile_camera(const CameraDescriptor&, const CameraCatalog*, CameraCompileContext&);

    // ── Runtime state ───────────────────────────────────────────────────
    CameraFailurePolicy        failure_policy_{CameraFailurePolicy::Stop};

    // ── Compiled state (populated by compile_camera()) ───────────────────
    bool                              compiled_{false};
    bool                              time_dependent_{false};
    CameraEvaluationDependency        evaluation_dependency_{
        CameraEvaluationDependency::Stateless};
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
};

} // namespace chronon3d::camera_v1
