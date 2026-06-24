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

/// Structured result from CameraProgram::evaluate().
struct CameraProgramResult {
    Camera2_5D                              camera;
    bool                                    ok{true};
    std::vector<CameraProgramDiagnostic>    diagnostics;
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

    /// Failure policy for constraint evaluation.
    CameraProgram& failure_policy(CameraFailurePolicy p) { failure_policy_ = p; return *this; }

    // ── Compiled evaluate ───────────────────────────────────────────────
    /// Evaluate the compiled program using a slim context (no base state
    /// duplication).  The base state is read from the internal descriptor.
    /// Only valid after compile_camera(); calls without compilation will
    /// produce an error diagnostic.
    CameraProgramResult evaluate(const CameraEvalContext& ctx,
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

    // ── Source evaluator output (Agente 1) ───────────────────────────────
    // Threads the *real* trajectory tangent + banking value from the
    // source evaluator (which knows the parametric direction) into the
    // orientation step.  Both fields are std::optional so non-trajectory
    // sources (PoseTracks / OrbitMotion / StaticCamera) leave them empty
    // and the orientation step falls back to the legacy POI-based contract
    // (never used as a "fake tangent").
    struct EvaluatedCameraSource {
        Camera2_5D             camera;
        std::optional<Vec3>    path_tangent;   ///< raw tangent from trajectory sample (orientation step normalises).
        std::optional<float>   path_roll_deg;  ///< trajectory sample's banking value for A1.5.
    };

    // ── A1.7 degenerate-tangent fallback cache ───────────────────────────
    // `evaluate()` is `const` so this fallback state lives on mutable
    // members.  Sequential CameraProgram evaluation only (one program per
    // render thread per AGENTS.md policy); concurrent evaluate() on the
    // same program is not a documented use case here and would race
    // this fallback but not affect correctness for any non-degenerate
    // tangent (which is the overwhelming majority).
    mutable Vec3  last_valid_path_tangent_{0.0f, 0.0f, 0.0f};
    mutable bool  has_last_path_tangent_{false};

    // ── Compiled evaluation helpers ─────────────────────────────────────
    /// Evaluate a source variant directly (no registry lookup).  Returns
    /// the per-source camera snapshot plus optional trajectory metadata
    /// (path_tangent + path_roll_deg) consumed by `apply_orientation_spec`
    /// below.
    EvaluatedCameraSource evaluate_compiled_source(const CameraEvalContext& ctx) const;

    /// Apply orientation from an OrientationSpec variant (passed as opaque
    /// ptr).  `path_tangent` / `path_roll_deg` are forwarded from the
    /// source evaluator; they are populated ONLY by TrajectoryMotion, and
    /// OrientAlongPath uses the real tangent when present (A1.3-A1.5).
    void apply_orientation_spec(const void* orient_variant,
                                const CameraEvalContext& ctx,
                                Camera2_5D& cam,
                                const std::optional<Vec3>& path_tangent,
                                const std::optional<float>& path_roll_deg) const;
};

} // namespace chronon3d::camera_v1
