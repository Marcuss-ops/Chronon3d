#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_program.hpp
//
// CameraProgram is the V1 *single evaluation entry-point*.
//
// The RECOMMENDED usage path is:
//   1. Build a CameraDescriptor
//   2. Call compile_camera(descriptor, program, ...)
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

#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D

#include <memory>
#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

// Forward declarations for types defined in camera_session.hpp
// which cannot be included here to avoid circular deps.
struct CameraSession;
struct CameraEvalContext;
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

private:
    friend bool compile_camera(const CameraDescriptor&, CameraProgram&,
                                CameraCompileError*, const CameraCatalog*);

    // ── Runtime state ───────────────────────────────────────────────────
    CameraFailurePolicy        failure_policy_{CameraFailurePolicy::Stop};

    // ── Compiled state (populated by compile_camera()) ───────────────────
    bool                       compiled_{false};
    bool                       time_dependent_{false};
    CameraDescriptor           descriptor_{};

    // ── Compiled evaluation helpers ─────────────────────────────────────
    /// Evaluate a source variant directly (no registry lookup).
    Camera2_5D evaluate_compiled_source(const CameraEvalContext& ctx) const;

    /// Apply orientation from an OrientationSpec variant (passed as opaque ptr).
    void apply_orientation_spec(const void* orient_variant,
                                const CameraEvalContext& ctx,
                                Camera2_5D& cam) const;
};

} // namespace chronon3d::camera_v1
