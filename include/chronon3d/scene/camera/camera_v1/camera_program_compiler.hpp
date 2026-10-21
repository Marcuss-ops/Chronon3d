#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_program_compiler.hpp
//
// CameraProgram compiler — compiles a CameraDescriptor into an immutable
// CameraProgram that can be evaluated without registry lookups.
//
// The compiler:
//   1. Validates the source (durations, keyframes, trajectory bounds)
//   3. Pre-computes arc-length tables for trajectories
//   4. Assigns constraint state slots
//   5. Flags whether the camera is time-dependent (for caching)
//   6. Produces a compiled CameraProgram with zero registry lookup in evaluate()
//
// Returns Result<CameraProgram, CameraCompileError>.
//
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/core/types/result.hpp>

#include <string>
#include <utility>

namespace chronon3d::camera_v1 {

// =============================================================================
// CameraCompileError — structured error from compile_camera().
//
// TICKET-PHASE-2: the previous nested `Kind` enum (20+ values) has been
// REMOVED.  The error code is now a top-level `CameraCompileErrorCode`
// (11 values, defined in camera_program.hpp) so that callers can pattern-
// match on the code without depending on the CameraCompileError type.
// Per AGENTS.md "no espansione API non necessaria", the old `Kind` enum
// was deleted (not aliased / not deprecated) to avoid a parallel enum
// surface that would invite drift between the two.
// =============================================================================

struct CameraCompileError {
    CameraCompileErrorCode code{CameraCompileErrorCode::InvalidDescriptor};
    std::string            message;
};

// Camera compiler compatibility boundary. CameraCompileErrorCode stays as
// the structured compiler detail while construction is adapter-owned.
[[nodiscard]] inline CameraCompileError make_camera_compile_error(
    CameraCompileErrorCode code, std::string message) {
    return CameraCompileError{code, std::move(message)};
}

// =============================================================================
// compile_camera() — compile a CameraDescriptor into a CameraProgram.
//
// Returns Result<CameraProgram, CameraCompileError>:
//   - On success: the compiled CameraProgram (implicitly from CameraProgram&&).
//   - On failure: a CameraCompileError (implicitly from CameraCompileError&&).
//
// Usage:
//   auto result = compile_camera(descriptor);
//   if (!result) { return result.error(); }
//   auto program = std::move(result).value();
//
//   // Or with TRY:
//   auto program = CHRONON_TRY(compile_camera(descriptor));
//
// The returned program is immutable and thread-safe:
//   - No registry lookups in evaluate()
//   - No mutex acquisitions
//   - No heap allocations (except for trajectory sampling)
//   - No string construction (except for diagnostics)
//
[[nodiscard]] chronon3d::Result<CameraProgram, CameraCompileError>
compile_camera(const CameraDescriptor& descriptor);

/// Compile and evaluate a descriptor at one sample for static scene adapters.
/// Render loops should retain CameraProgram and CameraSession instead.
[[nodiscard]] Camera2_5D evaluate_camera_descriptor(
    const CameraDescriptor& descriptor, const CameraEvalContext& context);

} // namespace chronon3d::camera_v1
