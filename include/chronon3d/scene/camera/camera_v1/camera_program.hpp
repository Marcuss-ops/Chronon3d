#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_program.hpp
//
// CameraProgram is the V1 *single evaluation entry-point*. Any composition
// that wants a camera goes through it:
//
//     auto result = CameraProgram{}
//         .motion("camera.hero_push")
//         .add_constraint("camera.look_at")
//         .orient(OrientationPolicy::OrientAlongPath)
//         .banking({.enabled=true, .strength=0.15f})
//         .evaluate(ctx, session);
//     if (!result.ok) { /* handle diagnostics */ }
//     scene.camera().set(result.camera);
//
// Orchestration layer over:
//   1. base cam (from scene.camera())
//   2. motion lookup   -> CameraMotionRegistry
//   3. trajectory sampling -> CameraTrajectory (optional)
//   4. orientation + banking -> tangent-based auto-orient + curvature roll
//   5. constraint stack     -> CameraConstraintRegistry
//   6. validation           -> CameraShotValidator (existing)
//
// COMPILED PATH (PR1+):
//   CameraProgram can be compiled via compile_camera() from a
//   CameraDescriptor.  After compilation, evaluate() takes a slim
//   CameraEvalContext (no duplicated base state) and a CameraSession,
//   and performs zero registry lookups.
//
// Both paths (builder-style + compiled) coexist during migration.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_constraint_resolver.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_trajectory.hpp>

// camera_descriptor.hpp provides CameraDescriptor, CameraFailurePolicy,
// StaticCameraSource, CameraSourceSpec, OrientationSpec, etc.
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>

#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D

#include <memory>
#include <string>
#include <variant>
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
// Source variant — prevents ambiguous states (motion + trajectory together).
// =========================================================================

/// Static source: no motion, camera stays at its base state.
// (Defined in camera_descriptor.hpp; re-declared here for backward compat.)
// using StaticCameraSource = chronon3d::camera_v1::StaticCameraSource; // same type

/// Registered motion reference (builder path).
struct RegisteredMotionSource { std::string id; bool valid{false}; };

/// Trajectory source (shared_ptr for compatibility).
struct TrajectorySource { std::shared_ptr<CameraTrajectory> trajectory; };

/// Sentinel for the compiled path: source was already resolved.
struct RefResolvedSource {};

using CameraProgramSource = std::variant<
    StaticCameraSource,
    RegisteredMotionSource,
    TrajectorySource,
    RefResolvedSource>;

// =========================================================================
// Orientation + banking
// =========================================================================

/// Optional orientation policy applied AFTER trajectory sampling.
enum class OrientationPolicy : std::uint8_t {
    FixedRotation = 0,            // keep camera's existing rotation
    LookAtPoint   = 1,            // orient toward ctx.base_target
    LookAtLayer   = 2,            // orient toward a named layer's transform
    OrientAlongPath = 3,          // align forward with trajectory tangent
    OrientAlongPathKeepHorizon = 4, // like OrientAlongPath but zero roll
    Custom        = 5,            // reserved for callback-based (Planned)
};

/// Banking controls — curvature-derived roll smoothed over time.
struct BankingSettings {
    bool  enabled{false};
    float strength{0.15f};       // how much curvature translates to roll
    float max_roll_deg{8.0f};    // hard clamp on roll angle
    float smoothing{0.8f};       // EMA factor for roll smoothing
};

// =========================================================================
// CameraProgram
// =========================================================================

class CameraProgram {
public:
    CameraProgram() = default;

    // ── Builder API (existing, unchanged) ───────────────────────────────

    /// Source-of-truth: the camera after motion / trajectory before constraints.
    CameraProgram& base(const Camera2_5D& cam) { base_ = cam; return *this; }
    const Camera2_5D& base_cam() const { return base_; }

    /// Motion preset id (mutually exclusive with trajectory).
    CameraProgram& motion(const std::string& id);

    /// Trajectory: if set, motion() is ignored.
    CameraProgram& trajectory(std::shared_ptr<CameraTrajectory> t);

    /// Constraints (registered in the order added).
    CameraProgram& add_constraint(std::shared_ptr<CameraConstraint> c);
    CameraProgram& add_constraint_named(const std::string& registry_id);

    /// Orientation policy applied AFTER trajectory sampling, BEFORE constraints.
    CameraProgram& orient(OrientationPolicy p) { orient_ = p; return *this; }

    /// Banking settings.
    CameraProgram& banking(BankingSettings b) { banking_ = b; return *this; }

    /// Failure policy for constraint evaluation.
    CameraProgram& failure_policy(CameraFailurePolicy p) { failure_policy_ = p; return *this; }

    // ── Source queries ──────────────────────────────────────────────────
    bool has_motion() const;
    bool has_trajectory() const;
    bool has_constraints() const { return !constraints_.empty(); }
    CameraProgramSource source() const { return source_; }

    // ── Legacy evaluate (unchanged) ─────────────────────────────────────
    /// One-shot evaluator. Returns structured result with diagnostics.
    CameraProgramResult evaluate(const CameraMotionContext& ctx,
                                 ConstraintSession& session) const;

    // ── NEW: Compiled evaluate (no registry lookup) ─────────────────────
    /// Evaluate the compiled program using a slim context (no base state
    /// duplication).  The base state is read from the internal descriptor.
    /// Only valid after compile_camera(); calls without compilation will
    /// produce an error diagnostic.
    CameraProgramResult evaluate(const CameraEvalContext& ctx,
                                 CameraSession& session) const;

    // ── NEW: Compiled state queries ─────────────────────────────────────
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

    // ── Existing builder state ──────────────────────────────────────────
    Camera2_5D                 base_{};
    CameraProgramSource        source_{StaticCameraSource{}};
    CameraConstraintStack      constraints_;
    OrientationPolicy          orient_{OrientationPolicy::FixedRotation};
    BankingSettings            banking_{};
    CameraFailurePolicy        failure_policy_{CameraFailurePolicy::Stop};

    // ── NEW: Compiled state (populated by compile_camera()) ─────────────
    bool                                    compiled_{false};
    bool                                    time_dependent_{false};
    CameraDescriptor                        descriptor_{};


    // ── Existing evaluation helpers ─────────────────────────────────────
    Camera2_5D sample_trajectory_cam(const CameraMotionContext& ctx,
                                     Vec3 fallback_target,
                                     ConstraintSession& session,
                                     CameraProgramResult& result) const;

    void apply_orientation(const CameraTrajectorySample& s,
                           const CameraMotionContext& ctx,
                           Camera2_5D& cam) const;

    float apply_banking(float roll_deg, const CameraMotionContext& ctx,
                        ConstraintSession& session) const;

    // ── NEW: Compiled evaluation helpers ────────────────────────────────
    // Private helpers for the compiled path use opaque void* parameters
    // because the concrete types (PoseTracksSource, OrbitMotion, CameraSourceSpec,
    // OrientationSpec) are defined in camera_descriptor.hpp, which includes us.
    // The implementations in camera_program.cpp reinterpret_cast them back.

    /// Evaluate a source variant directly (no registry lookup).
    Camera2_5D evaluate_compiled_source(const CameraEvalContext& ctx) const;

    /// Apply orientation from an OrientationSpec variant (passed as opaque ptr).
    void apply_orientation_spec(const void* orient_variant,
                                const CameraEvalContext& ctx,
                                Camera2_5D& cam) const;
};

} // namespace chronon3d::camera_v1
